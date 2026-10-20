// Unofficial Spotykach - own code, GNU AGPLv3 (see LICENSE).
// Not an official Synthux Academy product; not affiliated with Synthux Academy.
//
// Drives the upstream Spotykach core (third_party/spotykach, MIT, Copyright (c) 2026 Synthux
// Academy). See Engine.h for the design. What this file takes from the firmware's UI layer
// (src/ui/core.ui.cpp, core.ui.pads.cpp) is noted next to each piece.
#include "Engine.h"
#include "StreamResampler.h"
#include "EngineTime.h"
#include "PitchMap.h"

#include "core.h"        // upstream: src/core/core.h
#include "config.h"
#include "detector.h"
#include "fx/fx.h"
#include "track.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

namespace usp
{
    namespace
    {
        constexpr double kEngineRate   = 48000.0;
        constexpr int    kChunk        = 96;      // 2 ms at 48 kHz: the firmware's audio block / clock tick

        // One block of zeroed memory, carved into the pieces the core wants. Zeroed on purpose: the
        // firmware relies on zero-initialised static memory for members its constructors do not set.
        class Arena
        {
        public:
            explicit Arena (size_t bytes) : base ((unsigned char*) std::calloc (1, bytes)), size (bytes) {}
            ~Arena() { std::free (base); }
            Arena (const Arena&) = delete;
            Arena& operator= (const Arena&) = delete;

            template <typename T>
            T* take (size_t count)
            {
                const size_t align = 64;
                used = (used + align - 1) & ~(align - 1);
                const size_t bytes = count * sizeof (T);
                if (base == nullptr || used + bytes > size) return nullptr;
                auto* p = reinterpret_cast<T*> (base + used);
                used += bytes;
                return p;
            }
            bool ok() const { return base != nullptr; }

        private:
            unsigned char* base;
            size_t size, used = 0;
        };

        size_t sourceFrames() { return (size_t) 42 * 48000; }   // 42 s, as in the firmware (hw/buffer.sdram.h)

        size_t arenaBytes()
        {
            const size_t frames = sourceFrames() * sizeof (spotykach::Buffer::Frame) * 2;
            const size_t delays = 4 * (spotykach::Fx::kEchoDelayBufferLength * sizeof (float));
            const size_t detect = 4 * (spotykach::Detector::kWindow * sizeof (float));
            const size_t slices = 2 * (spotykach::kMaxSlicePointCount * sizeof (size_t));
            const size_t track  = 2 * (spotykach::Track::kLength * sizeof (spotykach::Event));
            return frames + delays + detect + slices + track + 64 * 16;
        }
    }

    struct Engine::Impl
    {
        explicit Impl (EngineStatus& s)
            : status (s), arena (arenaBytes())
        {
            // The core itself also lives in zeroed memory (see Arena).
            coreMemory = std::calloc (1, sizeof (spotykach::Core));
            if (! arena.ok() || coreMemory == nullptr) return;

            // `bufs` must outlive the core: Detector and Fx keep pointers INTO these arrays, not copies.
            auto& b = bufs;
            b = spotykach::Core::Buffers {};
            b.main_size = sourceFrames();
            for (int d = 0; d < spotykach::Deck::Count; ++d)
            {
                b.main[d] = arena.take<spotykach::Buffer::Frame> (sourceFrames());
                for (int c = 0; c < 2; ++c)
                {
                    b.detect[d][c] = arena.take<float> (spotykach::Detector::kWindow);
                    b.delay[d][c]  = arena.take<float> (spotykach::Fx::kEchoDelayBufferLength);
                }
                b.slices[d] = arena.take<size_t> (spotykach::kMaxSlicePointCount);
                b.track[d]  = arena.take<spotykach::Event> (spotykach::Track::kLength);
            }

            core = new (coreMemory) spotykach::Core();
            core->init ((float) kEngineRate, (float) kChunk, b);
            configureCore();
            ready = true;

            // One silent chunk so queued commands (deck mode) are applied before the first real use.
            std::vector<float> zi ((size_t) kChunk, 0.0f), zo ((size_t) kChunk, 0.0f);
            runEngine (zi.data(), zi.data(), zo.data(), zo.data(), kChunk);
            first = true;   // the first real call snaps to the host's values instead of gliding from defaults
        }

        ~Impl()
        {
            if (core != nullptr) core->~Core();
            std::free (coreMemory);
        }

        // ---- Setup: values the firmware's CoreUI::_init_values() / switch handlers provide -------------
        void configureCore()
        {
            using namespace spotykach;
            core->driver().set_on_quarter ([] (const bool) {});   // the Driver calls these unconditionally
            core->driver().set_on_clock_out ([] {});
            core->set_route (Route::Stereo);    // firmware leaves this unset; Stereo chosen (decision D7)
            core->set_mix (0.5f);
            core->set_click_mix (0.0f);
            core->mix_mod_in (0.0f);

            for (auto ref : { Deck::A, Deck::B })
            {
                auto& deck = core->deck (ref);
                deck.set_mode (Mode::Reel);      // must be set (and applied) before arming
                auto& g = deck.voxs();
                g.set_start (0.0f);
                g.set_start_offset_interval (0.0f);
                g.set_size (1.0f, false);
                g.set_speed (0.5f);
                g.set_shape (0.0f);
                g.set_env_size (1.0f);
                g.set_win_size (0.2f);
                g.set_start_mod_on (true);
                g.set_size_mod_on (true);
                deck.set_inout_mix (0.5f);
                deck.set_feedback (kDefaultFeedback);
            }
        }

        // ---- The firmware reads its CV inputs once per audio block; with nothing patched they read zero.
        void applyIdleCvInputs()
        {
            using namespace spotykach;
            for (auto ref : { Deck::A, Deck::B })
            {
                auto& deck = core->deck (ref);
                deck.inout_mix_mod_in (0.0f);
                deck.voxs().set_start_mod (0.0f);
                deck.voxs().set_size_mod (0.0f);
                deck.voxs().pitch_speed_mod_in (1.0f);   // 1.0 = no V/Oct offset
            }
            core->mix_mod_in (0.0f);
        }

        // ---- Parameters -------------------------------------------------------------------------------
        void setTargets (const EngineParams& p) { target = p; }

        void applyParams()
        {
            using namespace spotykach;
            constexpr float a = 0.12f;                    // ~16 ms smoothing at one update per 2 ms
            auto follow = [&] (float& cur, const float tgt)
            {
                cur = first ? tgt : cur + a * (tgt - cur);
            };
            for (int d = 0; d < 2; ++d)
            {
                auto& t = target.deck[d];
                auto& c = cur.deck[d];
                auto& l = last.deck[d];
                follow (c.position, t.position);
                follow (c.size,     t.size);
                follow (c.envelope, t.envelope);
                follow (c.feedback, t.feedback);
                // Pitch and mix are smoothed inside the core itself (speed glides over ~100 ms; Deck has a
                // one-pole smoother on the in/out mix), so they go in directly.
                c.pitch = t.pitch;
                c.mix   = t.mix;

                auto& deck = core->deck (d == 0 ? Deck::A : Deck::B);
                auto& g = deck.voxs();

                // Mode switch, as in CoreUI::_process_switches(): set the mode, re-infer the panner,
                // then re-apply size (and here also pitch/position/envelope) once the mode is in effect.
                if (first || t.mode != lastModeParam[d])
                {
                    lastModeParam[d] = t.mode;
                    deck.set_mode (t.mode == 1 ? Mode::Slice : t.mode == 2 ? Mode::Drift : Mode::Reel);
                    core->infer_panner_mode();
                }
                if (deck.mode() != appliedMode[d])
                {
                    appliedMode[d] = deck.mode();
                    l.position = l.size = l.pitch = l.envelope = -1.0f;
                }

                const float pitchValue = snapPitch (c.pitch, t.pitchSnap);
                if (changed (l.position, c.position)) g.set_start (c.position);
                if (changed (l.size, c.size))         g.set_size (c.size, false);
                if (changed (l.envelope, c.envelope)) g.set_shape (c.envelope);
                if (changed (l.pitch, pitchValue))
                {
                    if (deck.mode() == Mode::Slice) g.set_pitch (pitchValue);   // Slice: pitch without changing speed
                    else                            g.set_speed (pitchValue);   // Reel/Drift: tape speed
                }
                if (changed (l.mix, c.mix))           deck.set_inout_mix (c.mix);
                if (changed (l.feedback, c.feedback)) deck.set_feedback (c.feedback);
                if (changed (l.sliceOffset, t.sliceOffset)) g.set_start_offset_interval (t.sliceOffset);
                if (changed (l.envLength, t.envLength))     g.set_env_size (t.envLength);
                if (changed (l.window, t.window))           g.set_win_size (t.window);
            }

            // The core smooths the crossfade itself (OnePoleSmoother), so it goes in directly.
            if (changed (last.crossfade, target.crossfade)) core->set_mix (target.crossfade);
            if (first || target.route != last.route)
            {
                last.route = target.route;
                core->set_route (target.route == kRouteDoubleMono ? Route::DoubleMono
                               : target.route == kRouteGenerativeStereo ? Route::GenerativeStereo : Route::Stereo);
            }
            if (changed (last.panSpeed, target.panSpeed)) core->panner().set_speed (target.panSpeed);
            if (changed (last.panRange, target.panRange)) core->panner().set_range (target.panRange);
            if (changed (last.clickLevel, target.clickLevel)) core->set_click_mix (target.clickLevel);
            if (first || target.launchQuantise != last.launchQuantise)
            {
                last.launchQuantise = target.launchQuantise;
                core->driver().set_key_tick_interval_norm ((float) std::min (16, std::max (0, target.launchQuantise)) / 16.0f);
            }
            core->driver().set_tempo_norm (Tempo::abs_to_norm (std::min (250.0f, std::max (20.0f, effectiveBpm()))));
            first = false;
        }

        float effectiveBpm() const
        {
            return (target.hostSync && hostNow.hasTempo) ? (float) hostNow.bpm : target.tempoBpm;
        }

        // Host sync. While the host is playing and gives a beat position, the driver is switched to its
        // external (24 PPQN) source and fed one tick per 1/24 beat, so launch quantisation and slice
        // grids land on the host's beats. The lock is taken on the next whole beat. Otherwise the
        // internal clock runs at the host's tempo (or at the Tempo parameter when sync is off).
        // Returns true when this chunk carries an external tick.
        bool hostClockStep()
        {
            using namespace spotykach;
            auto& drv = core->driver();
            const bool wantExt = target.hostSync && hostNow.playing && hostNow.hasPpq && hostNow.hasTempo;
            if (! wantExt)
            {
                while (drv.source() != Driver::Source::internal) drv.toggle_source();
                clockLocked = false;
                ppqValid = false;
                hostFresh = false;
                status.hostLocked.store (false, std::memory_order_relaxed);
                return false;
            }

            const double step = (double) effectiveBpm() / 60.0 * (double) kChunk / kEngineRate;
            double prev = ppqEst;
            bool relock = false;
            if (! ppqValid || (hostFresh && std::abs (ppqEst - hostNow.ppq) > 0.1))
            {
                ppqEst = hostNow.ppq;
                prev = ppqEst - 1.0e-9;
                ppqValid = true;
                relock = true;
                if (clockLocked)
                {
                    clockLocked = false;                 // position jumped: drop back to internal until the next beat
                    while (drv.source() != Driver::Source::internal) drv.toggle_source();
                }
            }
            else
            {
                ppqEst += step;
            }
            hostFresh = false;

            const bool crossedBeat = std::floor (ppqEst) > std::floor (prev)
                                  || (relock && (ppqEst - std::floor (ppqEst)) < 0.01);
            if (! clockLocked)
            {
                if (! crossedBeat) return false;
                while (drv.source() != Driver::Source::midi) drv.toggle_source();   // internal -> ts4 -> midi (24 PPQN)
                clockLocked = true;
                status.hostLocked.store (true, std::memory_order_relaxed);
                return true;
            }
            return std::floor (ppqEst * 24.0) > std::floor (prev * 24.0);
        }

        bool changed (float& lastValue, const float v)
        {
            if (first || std::abs (v - lastValue) > 1.0e-5f)
            {
                lastValue = v;
                return true;
            }
            return false;
        }

        // ---- Button presses: same sequence as CoreUI::_toggle_play / _toggle_record -------------------------
        void applyActions (const uint32_t actions)
        {
            using namespace spotykach;
            for (int d = 0; d < 2; ++d)
            {
                const auto ref = d == 0 ? Deck::A : Deck::B;
                const uint32_t bits = (actions >> (d * 8)) & 0xffu;
                if (bits == 0) continue;
                auto& deck = core->deck (ref);

                auto record = [&] (const Deck::Source src)
                {
                    core->set_source (src, ref);
                    deck.toggle_recording();
                };
                if ((bits & kActionRecord) != 0)         record (Deck::Source::external);
                if ((bits & kActionRecordInternal) != 0) record (Deck::Source::internal);

                auto play = [&] (const bool reverse)
                {
                    deck.disarm();
                    if (! deck.is_overdubbing() && (! deck.is_playing() || deck.is_reverse() == reverse))
                        core->driver().toggle_play (ref);
                    deck.set_reverse (reverse);
                };
                if ((bits & kActionPlay) != 0)    play (false);
                if ((bits & kActionReverse) != 0) play (true);

                if ((bits & kActionTrig) != 0)
                {
                    // Same as CoreUI::_trigger(): a speed event with no V/Oct offset (speed 1).
                    auto e = make_event();
                    e.discont = false;
                    e.p3 = 1.0f;
                    e.p3_on = true;
                    deck.trigger (&e);
                }
            }
        }

        // ---- Engine-rate processing, split at the 96-sample tick boundaries ---------------------------------
        void runEngine (const float* inL, const float* inR, float* outL, float* outR, int m)
        {
            int done = 0;
            while (done < m)
            {
                if (chunkPos == 0) beginChunk();
                const int run = std::min (m - done, kChunk - chunkPos);
                const float* ins[2]  = { inL + done, inR + done };
                float*       outs[2] = { outL + done, outR + done };
                core->process (ins, outs, (size_t) run);
                chunkPos += run;
                done += run;
                if (chunkPos >= kChunk)
                {
                    chunkPos = 0;
                    engineSamples += (uint64_t) kChunk;
                }
            }
        }

        // Order on the hardware, per audio block: parameters (main loop), clock tick (CoreUI::tick),
        // prepare() (TIM5 timer), then Core::process.
        void beginChunk()
        {
            usp::gEngineMs = (uint32_t) (engineSamples / 48);
            applyIdleCvInputs();
            applyParams();
            core->driver().tick (hostClockStep());
            core->prepare();
        }

        void publish()
        {
            using namespace spotykach;
            status.tempoBpm.store (core->driver().tempo(), std::memory_order_relaxed);
            for (int d = 0; d < 2; ++d)
            {
                auto& deck = core->deck (d == 0 ? Deck::A : Deck::B);
                auto& s = status.deck[d];
                s.armed.store (deck.is_armed(), std::memory_order_relaxed);
                s.recording.store (deck.is_recording(), std::memory_order_relaxed);
                s.playing.store (deck.is_playing(), std::memory_order_relaxed);
                s.reverse.store (deck.is_reverse(), std::memory_order_relaxed);
                s.empty.store (deck.is_empty(), std::memory_order_relaxed);
                s.loopSeconds.store ((float) deck.buffer().rec_size() / (float) kEngineRate, std::memory_order_relaxed);
                s.playhead.store (deck.norm_playhead_at (0), std::memory_order_relaxed);
            }
        }

        void prepare (const double hostRate, const int maxHostBlock)
        {
            (void) maxHostBlock;
            hostRateValue = hostRate;
            const double ratio = kEngineRate / hostRate;
            const double stepOut = ratio;
            slice = std::max (16, std::min (256, (int) (3000.0 / std::max (ratio, 1.0))));
            engineCap = (int) std::ceil ((double) slice * ratio) + 64;

            for (auto& v : scratchIn)  v.assign ((size_t) slice, 0.0f);
            for (auto& v : engIn)      v.assign ((size_t) engineCap, 0.0f);
            for (auto& v : engOut)     v.assign ((size_t) engineCap, 0.0f);

            rsIn.prepare (hostRate, kEngineRate, slice);
            rsOut.prepare (kEngineRate, hostRate, engineCap);
            // The input converter yields nothing until ~kTaps frames have arrived, so the output converter
            // needs enough silent priming to keep supplying frames meanwhile. Without it every missing
            // frame would become permanent extra delay. Priming is the only latency this adds beyond
            // the two filters' own delay.
            outPrime = StreamResampler::kTaps + (int) std::ceil ((double) StreamResampler::kTaps * stepOut) + 8;
            rsIn.reset (0);
            rsOut.reset (outPrime);
        }

        void process (const float* inL, const float* inR, float* outL, float* outR, int n,
                      const EngineParams& params, const uint32_t actions, const HostTransport& host)
        {
            if (! ready)
            {
                if (outL != inL) std::memcpy (outL, inL, (size_t) n * sizeof (float));
                if (outR != inR) std::memcpy (outR, inR, (size_t) n * sizeof (float));
                return;
            }

            usp::gEngineMs = (uint32_t) (engineSamples / 48);
            setTargets (params);
            hostNow = host;
            hostFresh = true;
            applyActions (actions);

            int off = 0;
            while (off < n)
            {
                const int s = std::min (n - off, slice);
                std::memcpy (scratchIn[0].data(), inL + off, (size_t) s * sizeof (float));
                std::memcpy (scratchIn[1].data(), inR + off, (size_t) s * sizeof (float));

                if (rsIn.isBypass())
                {
                    runEngine (scratchIn[0].data(), scratchIn[1].data(), outL + off, outR + off, s);
                }
                else
                {
                    rsIn.push (scratchIn[0].data(), scratchIn[1].data(), s);
                    const int m = std::min (rsIn.available(), engineCap);
                    rsIn.pull (engIn[0].data(), engIn[1].data(), m);
                    runEngine (engIn[0].data(), engIn[1].data(), engOut[0].data(), engOut[1].data(), m);
                    rsOut.push (engOut[0].data(), engOut[1].data(), m);
                    const int got = rsOut.pull (outL + off, outR + off, s);
                    if (got < s)
                    {
                        std::memset (outL + off + got, 0, (size_t) (s - got) * sizeof (float));
                        std::memset (outR + off + got, 0, (size_t) (s - got) * sizeof (float));
                        status.underruns.fetch_add ((uint32_t) (s - got), std::memory_order_relaxed);
                    }
                }
                off += s;
            }
            publish();
        }

        int latency() const
        {
            if (rsIn.isBypass()) return 0;
            // Output side: (priming - 15) engine frames, converted to host frames. Input side: the filter
            // is centred 15 frames ahead of the newest tap, which advances the stream by ~15 host frames.
            // Verified against measured delay by tests/engine_test.cpp.
            const double stepOut = kEngineRate / hostRateValue;
            return std::max (0, (int) std::ceil ((double) (outPrime - 15) / stepOut - 15.0));
        }

        EngineStatus& status;
        Arena arena;
        void* coreMemory = nullptr;
        spotykach::Core::Buffers bufs {};
        spotykach::Core* core = nullptr;
        bool ready = false;

        EngineParams target, cur, last;
        HostTransport hostNow;
        bool hostFresh = false, clockLocked = false, ppqValid = false;
        double ppqEst = 0.0;
        int lastModeParam[2] = { -1, -1 };
        spotykach::Mode appliedMode[2] = { spotykach::Mode::None, spotykach::Mode::None };
        bool first = true;

        int chunkPos = 0;
        uint64_t engineSamples = 0;

        double hostRateValue = 48000.0;
        int outPrime = 0;
        int slice = 256, engineCap = 320;
        StreamResampler rsIn, rsOut;
        std::vector<float> scratchIn[2], engIn[2], engOut[2];
    };

    Engine::Engine() : impl (std::make_unique<Impl> (statusValues))
    {
        impl->prepare (48000.0, 512);
    }
    Engine::~Engine() = default;

    void Engine::prepare (double hostSampleRate, int maxHostBlock) { impl->prepare (hostSampleRate, maxHostBlock); }
    int  Engine::latencySamples() const { return impl->latency(); }
    void Engine::process (const float* inL, const float* inR, float* outL, float* outR, int n,
                          const EngineParams& params, uint32_t actions, const HostTransport& host)
    {
        impl->process (inL, inR, outL, outR, n, params, actions, host);
    }
}

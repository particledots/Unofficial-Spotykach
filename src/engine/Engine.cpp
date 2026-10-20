// Unofficial Spotykach - own code, GNU AGPLv3 (see LICENSE).
// Not an official Synthux Academy product; not affiliated with Synthux Academy.
//
// Drives the upstream Spotykach core (third_party/spotykach, MIT, Copyright (c) 2026 Synthux
// Academy). See Engine.h for the design. What this file takes from the firmware's UI layer
// (src/ui/core.ui.cpp, core.ui.pads.cpp) is noted next to each piece.
#include "Engine.h"
#include "StreamResampler.h"
#include "EngineTime.h"

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
            core->set_mix (0.0f);                // 0 = Deck A only (Deck B is not exposed yet)
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
            follow (cur.position, target.position);
            follow (cur.size,     target.size);
            follow (cur.envelope, target.envelope);
            follow (cur.feedback, target.feedback);
            // Pitch and mix are smoothed inside the core itself (speed glides over ~100 ms; Deck has a
            // one-pole smoother on the in/out mix), so they go in directly.
            cur.pitch = target.pitch;
            cur.mix   = target.mix;

            auto& deck = core->deck (Deck::A);
            auto& g = deck.voxs();
            if (changed (last.position, cur.position)) g.set_start (cur.position);
            if (changed (last.size, cur.size))         g.set_size (cur.size, false);
            if (changed (last.envelope, cur.envelope)) g.set_shape (cur.envelope);
            if (changed (last.pitch, cur.pitch))       g.set_speed (cur.pitch);   // Reel: speed (Slice would use set_pitch)
            if (changed (last.mix, cur.mix))           deck.set_inout_mix (cur.mix);
            if (changed (last.feedback, cur.feedback)) deck.set_feedback (cur.feedback);
            first = false;
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
            auto& deck = core->deck (Deck::A);
            if ((actions & kActionRecord) != 0)
            {
                core->set_source (Deck::Source::external, Deck::A);
                deck.toggle_recording();
            }
            auto play = [&] (const bool reverse)
            {
                deck.disarm();
                if (! deck.is_overdubbing() && (! deck.is_playing() || deck.is_reverse() == reverse))
                    core->driver().toggle_play (Deck::A);
                deck.set_reverse (reverse);
            };
            if ((actions & kActionPlay) != 0)    play (false);
            if ((actions & kActionReverse) != 0) play (true);
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
            core->driver().tick (false);
            core->prepare();
        }

        void publish()
        {
            using namespace spotykach;
            auto& deck = core->deck (Deck::A);
            status.armed.store (deck.is_armed(), std::memory_order_relaxed);
            status.recording.store (deck.is_recording(), std::memory_order_relaxed);
            status.playing.store (deck.is_playing(), std::memory_order_relaxed);
            status.reverse.store (deck.is_reverse(), std::memory_order_relaxed);
            status.empty.store (deck.is_empty(), std::memory_order_relaxed);
            status.loopSeconds.store ((float) deck.buffer().rec_size() / (float) kEngineRate, std::memory_order_relaxed);
            status.playhead.store (deck.norm_playhead_at (0), std::memory_order_relaxed);
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
                      const EngineParams& params, const uint32_t actions)
        {
            if (! ready)
            {
                if (outL != inL) std::memcpy (outL, inL, (size_t) n * sizeof (float));
                if (outR != inR) std::memcpy (outR, inR, (size_t) n * sizeof (float));
                return;
            }

            usp::gEngineMs = (uint32_t) (engineSamples / 48);
            setTargets (params);
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
                          const EngineParams& params, uint32_t actions)
    {
        impl->process (inL, inR, outL, outR, n, params, actions);
    }
}

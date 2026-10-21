// Unofficial Spotykach - own code, GNU AGPLv3 (see LICENSE).
// Headless test of the engine wrapper (no JUCE, no audio device). Run by CI; a failure fails the build.
#include "../src/engine/PitchMap.h"
#include "../src/engine/ModMap.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "lfo.h"
#include "Engine.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

// With -DCOUNT_ALLOCS every heap allocation in the program is counted, so the test can prove that
// Engine::process() allocates nothing (a hard rule for the audio thread). Not used with sanitizers.
#ifdef COUNT_ALLOCS
static std::atomic<long> gAllocs { 0 };
void* operator new (std::size_t n) { ++gAllocs; if (void* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[] (std::size_t n) { ++gAllocs; if (void* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
static long allocCount() { return gAllocs.load(); }
#else
static long allocCount() { return 0; }
#endif
static long gAudioThreadAllocs = 0;

using namespace usp;

static int failures = 0;
#define CHECK(cond, ...)                                           \
    do { if (! (cond)) { ++failures; std::printf ("  FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

// Runs `frames` of mono-duplicated input through the engine in blocks of `block`; returns left output.
struct Rig
{
    Engine engine;
    double rate;
    int block;
    EngineParams params;
    std::vector<float> lastOutR;                 // right output of the last run()
    HostTransport host;                          // advanced by run() while host.playing
    const std::vector<float>* rightIn = nullptr; // if set, used as right input (same length as the left input)
    int noteOn[2] = { -1, -1 };                  // MIDI notes for the first block of the next run(); cleared after use

    Rig (double r, int b) : rate (r), block (b) { params.crossfade = 0.0f; engine.prepare (r, b); }

    std::vector<float> run (const std::vector<float>& in, uint32_t actionsAtStart = 0)
    {
        std::vector<float> out (in.size(), 0.0f), outR (in.size(), 0.0f), inR = rightIn != nullptr ? *rightIn : in;
        std::vector<float> inCopy = in;
        size_t off = 0;
        bool first = true;
        while (off < in.size())
        {
            const int n = (int) std::min ((size_t) block, in.size() - off);
            const long before = allocCount();
            engine.process (inCopy.data() + off, inR.data() + off, out.data() + off, outR.data() + off, n,
                            params, first ? actionsAtStart : 0u, host, first ? noteOn : nullptr);
            if (host.playing) host.ppq += (double) n / rate * host.bpm / 60.0;
            gAudioThreadAllocs += allocCount() - before;
            first = false;
            off += (size_t) n;
        }
        lastOutR = outR;
        noteOn[0] = noteOn[1] = -1;
        return out;
    }
};

static std::vector<float> sine (double rate, double seconds, double freq, float amp)
{
    std::vector<float> v ((size_t) (rate * seconds));
    for (size_t i = 0; i < v.size(); ++i) v[i] = amp * (float) std::sin (2.0 * 3.14159265358979 * freq * (double) i / rate);
    return v;
}
static std::vector<float> silence (double rate, double seconds) { return std::vector<float> ((size_t) (rate * seconds), 0.0f); }

static bool finiteAndBounded (const std::vector<float>& v)
{
    for (float x : v) if (! std::isfinite (x) || std::abs (x) > 2.0f) return false;
    return true;
}
static double rms (const std::vector<float>& v, size_t a, size_t b)
{
    if (b > v.size()) b = v.size();
    if (b <= a) return 0.0;
    double s = 0;
    for (size_t i = a; i < b; ++i) s += (double) v[i] * v[i];
    return std::sqrt (s / (double) (b - a));
}

static void testLatency (double rate, int block)
{
    Rig r (rate, block);
    r.params.deck[0].mix = 0.0f;   // input only
    auto in = silence (rate, 0.5);
    const size_t at = (size_t) (rate * 0.1);
    in[at] = 0.25f;
    auto out = r.run (in);
    size_t peak = 0; float best = 0;
    for (size_t i = 0; i < out.size(); ++i) if (std::abs (out[i]) > best) { best = std::abs (out[i]); peak = i; }
    const int measured = (int) peak - (int) at;
    std::printf ("  latency: reported %d, measured %d samples (peak %.3f)\n", r.engine.latencySamples(), measured, best);
    CHECK (best > 0.05f, "impulse did not come through (peak %.4f)", best);
    CHECK (std::abs (measured - r.engine.latencySamples()) <= 3, "reported latency %d differs from measured %d", r.engine.latencySamples(), measured);
}

static void testRecordPlay (double rate, int block)
{
    Rig r (rate, block);
    r.params.deck[0].mix = 1.0f;       // loop only, so we hear nothing unless the loop plays
    const double recSeconds = 1.0;

    // 0.3 s of silence, arm record, 1.0 s of tone, press Play (ends recording), 3 s of silence
    auto a = silence (rate, 0.3);
    r.run (a);
    r.run (silence (rate, 0.05), kActionRecord);
    r.run (sine (rate, recSeconds, 220.0, 0.5f));
    r.run (silence (rate, 0.05), kActionPlay);
    auto tail = r.run (silence (rate, 3.0));

    const auto& s = r.engine.status();
    std::printf ("  loop: %.3f s, playing=%d, empty=%d, underruns=%u\n", s.deck[0].loopSeconds.load(), (int) s.deck[0].playing.load(), (int) s.deck[0].empty.load(), (unsigned) s.underruns.load());
    CHECK (finiteAndBounded (tail), "output has NaN/inf or is out of range");
    CHECK (! s.deck[0].empty.load(), "nothing was recorded");
    CHECK (s.deck[0].playing.load(), "deck is not playing");
    CHECK (std::abs (s.deck[0].loopSeconds.load() - recSeconds) < 0.15, "loop length %.3f s is not near %.2f s", s.deck[0].loopSeconds.load(), recSeconds);
    CHECK (s.underruns.load() == 0, "resampler under-ran %u frames", (unsigned) s.underruns.load());
    const double level = rms (tail, (size_t) (rate * 0.5), tail.size());
    CHECK (level > 0.05, "playback is too quiet (rms %.4f)", level);
}


// Both decks, crossfade, routing, internal recording, trig.
static void testDecks (double rate, int block)
{
    const auto tone = sine (rate, 1.0, 330.0, 0.5f);
    const auto none = silence (rate, 1.0);
    const uint32_t recA = deckAction (0, kActionRecord), recB = deckAction (1, kActionRecord);
    const uint32_t playA = deckAction (0, kActionPlay), playB = deckAction (1, kActionPlay);
    auto level = [&] (const std::vector<float>& v) { return rms (v, v.size() / 4, v.size()); };

    {   // Deck B on its own: audible with the crossfade at B, silent with it at A.
        Rig r (rate, block);
        r.params.deck[0].mix = 1.0f; r.params.deck[1].mix = 1.0f;
        r.run (silence (rate, 0.2));
        r.run (silence (rate, 0.05), recB);
        r.run (tone);
        r.run (silence (rate, 0.05), recB);
        r.run (silence (rate, 0.05), playB);
        r.params.crossfade = 1.0f;
        r.run (silence (rate, 0.3));
        const double atB = level (r.run (silence (rate, 1.5)));
        r.params.crossfade = 0.0f;
        r.run (silence (rate, 0.3));
        const double atA = level (r.run (silence (rate, 1.0)));
        const auto& s = r.engine.status();
        std::printf ("  deck B: crossfade@B rms %.4f, crossfade@A rms %.5f, loop %.2f s, A empty=%d\n", atB, atA,
                    s.deck[1].loopSeconds.load(), (int) s.deck[0].empty.load());
        CHECK (atB > 0.05, "deck B loop not audible with crossfade at B (rms %.4f)", atB);
        CHECK (atA < 1e-3, "deck B leaks with crossfade at A (rms %.5f)", atA);
        CHECK (std::abs (s.deck[1].loopSeconds.load() - 1.0f) < 0.15f, "deck B loop length %.3f", s.deck[1].loopSeconds.load());
        CHECK (s.deck[0].empty.load(), "deck A should still be empty");
        CHECK (s.underruns.load() == 0, "underruns %u", (unsigned) s.underruns.load());
    }
    {   // Double Mono: left input -> deck A (left output), right input -> deck B (right output).
        Rig r (rate, block);
        r.params.route = kRouteDoubleMono;
        r.params.crossfade = 0.5f;
        r.params.deck[0].mix = 1.0f; r.params.deck[1].mix = 1.0f;
        r.run (silence (rate, 0.2));
        r.rightIn = &none;
        r.run (silence (rate, 0.05), recA | recB);
        r.run (tone);                                  // left carries the tone, right is silent
        r.run (silence (rate, 0.05), recA | recB);
        r.run (silence (rate, 0.05), playA | playB);
        r.rightIn = nullptr;
        r.run (silence (rate, 0.3));
        auto l = r.run (silence (rate, 1.5));
        const double lL = level (l), lR = level (r.lastOutR);
        std::printf ("  double mono: left (deck A) rms %.4f, right (deck B) rms %.5f\n", lL, lR);
        CHECK (lL > 0.03, "double mono: deck A not heard on the left (rms %.4f)", lL);
        CHECK (lR < 1e-3, "double mono: deck B (silent input) leaked to the right (rms %.5f)", lR);
    }
    {   // Record deck B from deck A's output (internal source), then play B alone.
        Rig r (rate, block);
        r.params.deck[0].mix = 1.0f; r.params.deck[1].mix = 1.0f;
        r.run (silence (rate, 0.2));
        r.run (silence (rate, 0.05), recA);
        r.run (tone);
        r.run (silence (rate, 0.05), recA);
        r.run (silence (rate, 0.05), playA);
        r.run (silence (rate, 0.3));
        r.run (silence (rate, 0.05), deckAction (1, kActionRecordInternal));
        r.run (silence (rate, 2.2));                     // more than one loop of A goes into B
        r.run (silence (rate, 0.05), deckAction (1, kActionRecordInternal));
        r.run (silence (rate, 0.05), playA | playB);     // stop A, start B
        r.params.crossfade = 1.0f;
        r.run (silence (rate, 0.3));
        const double b = level (r.run (silence (rate, 1.0)));
        std::printf ("  internal record: deck B rms %.4f, B loop %.2f s, A playing=%d\n", b,
                    r.engine.status().deck[1].loopSeconds.load(), (int) r.engine.status().deck[0].playing.load());
        CHECK (b > 0.03, "deck B recorded from deck A is not audible (rms %.4f)", b);
    }
    {   // Trig and Generative Stereo must stay finite and keep playing.
        Rig r (rate, block);
        r.params.deck[0].mix = 1.0f;
        r.params.route = kRouteGenerativeStereo;
        r.run (silence (rate, 0.2));
        r.run (silence (rate, 0.05), recA);
        r.run (tone);
        r.run (silence (rate, 0.05), recA);
        r.run (silence (rate, 0.05), playA);
        auto o = r.run (silence (rate, 0.5), deckAction (0, kActionTrig));
        CHECK (finiteAndBounded (o) && finiteAndBounded (r.lastOutR), "trig/generative output not finite");
        o = r.run (silence (rate, 0.5));
        CHECK (r.engine.status().deck[0].playing.load(), "deck A stopped after trig");
        CHECK (finiteAndBounded (o), "output not finite after trig");
    }
}

static int zeroCrossings (const std::vector<float>& v, size_t a, size_t b)
{
    int n = 0;
    for (size_t i = a + 1; i < b; ++i) if ((v[i - 1] < 0.0f) != (v[i] < 0.0f)) ++n;
    return n;
}

// Slice and Drift modes, pitch snap, host sync.
static void testModesTempoPitch (double rate, int block)
{
    const auto tone = sine (rate, 2.0, 330.0, 0.5f);
    auto level = [&] (const std::vector<float>& v) { return rms (v, v.size() / 4, v.size()); };
    const uint32_t recA = deckAction (0, kActionRecord), playA = deckAction (0, kActionPlay);

    for (int mode = 1; mode <= 2; ++mode)    // 1 = Slice, 2 = Drift
    {
        Rig r (rate, block);
        r.params.deck[0].mode = mode;
        r.params.deck[0].mix = 1.0f;
        r.params.deck[0].envelope = mode == 2 ? 0.5f : 0.0f;   // Drift is silent with the envelope off
        r.run (silence (rate, 0.3));
        r.run (silence (rate, 0.05), recA);
        r.run (tone);
        r.run (silence (rate, 0.05), recA);
        r.run (silence (rate, 0.05), playA);
        r.run (silence (rate, 0.5));
        auto o = r.run (silence (rate, 2.0));
        const double lv = level (o);
        const auto& s = r.engine.status();
        std::printf ("  %s mode: rms %.4f, loop %.2f s, playing=%d, underruns=%u\n", mode == 1 ? "slice" : "drift", lv,
                    s.deck[0].loopSeconds.load(), (int) s.deck[0].playing.load(), (unsigned) s.underruns.load());
        CHECK (finiteAndBounded (o), "mode %d output not finite", mode);
        CHECK (! s.deck[0].empty.load(), "mode %d recorded nothing", mode);
        CHECK (lv > 0.01, "mode %d is silent (rms %.5f)", mode, lv);
        CHECK (s.underruns.load() == 0, "underruns %u", (unsigned) s.underruns.load());
        // switch back to Reel and through all modes while playing: must stay finite
        for (int m : { 0, 2, 1, 0 })
        {
            r.params.deck[0].mode = m;
            auto o2 = r.run (silence (rate, 0.4));
            CHECK (finiteAndBounded (o2), "output not finite after switching to mode %d", m);
        }
    }

    {   // Pitch snap: +12 semitones via the snapped knob must sound one octave up, like the free knob at the equivalent value.
        auto octave = [&] (int snap, float pitch)
        {
            Rig r (rate, block);
            r.params.deck[0].mix = 1.0f;
            r.run (silence (rate, 0.3));
            r.run (silence (rate, 0.05), recA);
            r.run (tone);
            r.run (silence (rate, 0.05), recA);
            r.params.deck[0].pitchSnap = snap;
            r.params.deck[0].pitch = pitch;
            r.run (silence (rate, 0.05), playA);
            r.run (silence (rate, 0.5));
            auto o = r.run (silence (rate, 0.5));
            return zeroCrossings (o, 0, o.size());
        };
        const int base = octave (0, 0.5f), up = octave (2, 0.75f), steps = octave (1, 0.68f);   // 0.68 -> step index 4 -> +7 st
        std::printf ("  pitch: unity %d crossings/0.5 s, snapped +12 st %d, steps(+7 st) %d\n", base, up, steps);
        CHECK (std::abs (up - 2 * base) < base / 10 + 6, "snapped +12 st is not an octave up (%d vs 2x%d)", up, base);
        const double fifth = std::pow (2.0, 7.0 / 12.0);
        CHECK (std::abs (steps - fifth * base) < base / 10 + 6, "step +7 st wrong (%d vs %.0f)", steps, fifth * base);
    }

    {   // Host sync: locks on the next beat while the host plays, reports the host tempo, drops out when it stops.
        Rig r (rate, block);
        r.params.hostSync = true;
        r.params.tempoBpm = 90.0f;
        r.host.hasTempo = true; r.host.hasPpq = true; r.host.bpm = 140.0; r.host.ppq = 3.3; r.host.playing = true;
        r.run (silence (rate, 0.1));
        const bool earlyLock = r.engine.status().hostLocked.load();
        r.run (silence (rate, 1.5));
        const auto& s = r.engine.status();
        std::printf ("  host sync: locked=%d (early %d), tempo %.1f\n", (int) s.hostLocked.load(), (int) earlyLock, s.tempoBpm.load());
        CHECK (s.hostLocked.load(), "host sync did not lock");
        CHECK (std::abs (s.tempoBpm.load() - 140.0f) < 1.0f, "engine tempo %.2f, expected the host's 140", s.tempoBpm.load());
        r.host.playing = false;
        r.run (silence (rate, 0.3));
        CHECK (! r.engine.status().hostLocked.load(), "still locked after the host stopped");
        r.params.hostSync = false;
        r.run (silence (rate, 0.3));
        CHECK (std::abs (r.engine.status().tempoBpm.load() - 90.0f) < 1.0f, "internal tempo %.2f, expected 90", r.engine.status().tempoBpm.load());
    }
}

// Flux (delay) timing in both modes, and Grit.
static void testFx (double rate, int block)
{
    auto impulseRun = [&] (Rig& r, double seconds)
    {
        std::vector<float> in = silence (rate, seconds);
        in[(size_t) (0.2 * rate)] = 0.8f;             // impulse at 0.2 s
        return r.run (in);
    };
    // time of the largest peak after the impulse (skipping the direct sound)
    auto echoDelay = [&] (const std::vector<float>& o)
    {
        const size_t imp = (size_t) (0.2 * rate);
        size_t first = imp;
        for (size_t i = imp; i < o.size(); ++i) if (std::abs (o[i]) > 0.1f) { first = i; break; }   // direct sound
        size_t best = 0; float bv = 0.0f;
        for (size_t i = first + (size_t) (0.05 * rate); i < o.size(); ++i) if (std::abs (o[i]) > bv) { bv = std::abs (o[i]); best = i; }
        return std::make_pair ((double) (best - first) / rate, bv);
    };

    {   // Free mode: time = 0.01 + t * 1.99 s
        Rig r (rate, block);
        r.params.deck[0].mix = 0.0f;                  // input only: the effect works on the pass-through
        r.params.deck[0].fluxOn = true;
        r.params.deck[0].fluxMode = 0;
        r.params.deck[0].fluxTime = 0.25f;
        r.params.deck[0].fluxFeedback = 0.0f;
        r.params.deck[0].fluxMix = 1.0f;
        r.run (silence (rate, 3.5));                  // Free mode glides to a new delay time (0.5 s lag): let it settle
        auto o = impulseRun (r, 1.6);
        auto e = echoDelay (o);
        std::printf ("  flux free: echo at %.4f s (expected %.4f), peak %.3f\n", e.first, 0.01 + 0.25 * 1.99, e.second);
        CHECK (std::abs (e.first - (0.01 + 0.25 * 1.99)) < 0.01, "free echo at %.4f s", e.first);
        CHECK (e.second > 0.05, "free echo too quiet (%.3f)", e.second);
    }
    for (double bpm : { 60.0, 90.0 })   // Clocked: 1/4 note must follow the ENGINE tempo, not a fixed 120 BPM
    {
        Rig r (rate, block);
        r.params.hostSync = false;
        r.params.tempoBpm = (float) bpm;
        r.params.deck[0].mix = 0.0f;
        r.params.deck[0].fluxOn = true;
        r.params.deck[0].fluxMode = 1;
        r.params.deck[0].fluxTime = 5.0f / 7.0f;      // index 5 = 1/4 note
        r.params.deck[0].fluxFeedback = 0.0f;
        r.params.deck[0].fluxMix = 1.0f;
        r.run (silence (rate, 0.3));
        auto o = impulseRun (r, 1.9);
        auto e = echoDelay (o);
        const double want = 60.0 / bpm;
        std::printf ("  flux clocked @%.0f BPM: echo at %.4f s (expected %.4f)\n", bpm, e.first, want);
        CHECK (std::abs (e.first - want) < 0.012, "clocked echo at %.4f s, expected %.4f", e.first, want);
    }
    {   // Grit: each mode changes the sound, stays finite and bounded; off is a clean pass-through.
        const auto tone = sine (rate, 0.6, 220.0f, 0.6f);
        std::vector<float> off;
        for (int mode = -1; mode <= 2; ++mode)       // -1 = grit off
        {
            Rig r (rate, block);
            r.params.deck[0].mix = 0.0f;
            r.params.deck[0].gritOn = mode >= 0;
            r.params.deck[0].gritMode = mode < 0 ? 0 : mode;
            r.params.deck[0].gritIntensity = 1.0f;
            r.params.deck[0].gritMix = 1.0f;
            r.params.deck[0].gritCharacter = 0.5f;
            r.run (silence (rate, 0.3));
            auto o = r.run (tone);
            CHECK (finiteAndBounded (o), "grit mode %d not finite", mode);
            if (mode < 0) { off = o; continue; }
            std::vector<float> diff (o.size());
            for (size_t i = 0; i < o.size(); ++i) diff[i] = o[i] - off[i];
            const double dv = rms (diff, o.size() / 3, o.size());
            std::printf ("  grit mode %d: rms of difference from clean %.4f\n", mode, dv);
            CHECK (dv > 0.01, "grit mode %d did not change the sound (difference rms %.5f)", mode, dv);
        }
    }
}


// Modulators: rate, sync, follower, each target, and the trigger sequencer.
static void testModulatorsAndSequencer (double rate, int block)
{
    const uint32_t recA = deckAction (0, kActionRecord), playA = deckAction (0, kActionPlay);
    const auto tone = sine (rate, 1.0, 330.0, 0.5f);
    auto loopPlaying = [&] (Rig& r, int mode = 0)
    {
        r.params.deck[0].mix = 1.0f;
        r.params.deck[0].mode = mode;
        r.run (silence (rate, 0.3));
        r.run (silence (rate, 0.05), recA);
        r.run (tone);
        r.run (silence (rate, 0.05), recA);
        r.run (silence (rate, 0.05), playA);
        r.run (silence (rate, 0.5));
    };
    // Counts upward crossings of 0.5 in the modulator output, sampled every 10 ms.
    auto crossings = [&] (Rig& r, double seconds)
    {
        int n = 0; float prev = r.engine.status().modValue[0].load();
        for (int i = 0; i < (int) (seconds * 100); ++i)
        {
            r.run (silence (rate, 0.01));
            const float v = r.engine.status().modValue[0].load();
            if (prev < 0.5f && v >= 0.5f) ++n;
            prev = v;
        }
        return n;
    };

    {   // free-running LFO: cycle 0.5 -> curved 0.5007 -> 0.01 + 0.5007 * 11.99 = 6.01 Hz
        Rig r (rate, block);
        r.params.mod[0].type = kModSine; r.params.mod[0].cycle = 0.5f; r.params.mod[0].glow = 1.0f;
        r.run (silence (rate, 0.3));
        const int n = crossings (r, 5.0);
        std::printf ("  LFO free: %d cycles in 5 s (expect ~30)\n", n);
        CHECK (std::abs (n - 30) <= 2, "free LFO counted %d cycles in 5 s, expected ~30", n);
    }
    {   // tempo-synced LFO: cycle 0.5 -> division index 3 -> 4 sixteenths = a quarter note -> 2 Hz at 120 BPM
        Rig r (rate, block);
        r.params.tempoBpm = 120.0f;
        r.params.mod[0].type = kModSine; r.params.mod[0].cycle = 0.5f; r.params.mod[0].glow = 1.0f; r.params.mod[0].sync = true;
        r.run (silence (rate, 0.6));
        const int n = crossings (r, 5.0);
        r.params.tempoBpm = 60.0f;
        r.run (silence (rate, 0.6));
        const int n2 = crossings (r, 5.0);
        std::printf ("  LFO synced: %d cycles in 5 s at 120 BPM (expect ~10), %d at 60 BPM (expect ~5)\n", n, n2);
        CHECK (std::abs (n - 10) <= 2, "synced LFO counted %d at 120 BPM, expected ~10", n);
        CHECK (std::abs (n2 - 5) <= 2, "synced LFO counted %d at 60 BPM, expected ~5", n2);
        r.params.mod[0].cycle = 1.0f;   // the upstream division table must not be indexed out of range
        auto o = r.run (silence (rate, 0.5));
        CHECK (finiteAndBounded (o), "synced LFO at cycle 1.0 is not finite");
    }
    {   // follower: moves with the deck's output
        Rig r (rate, block);
        r.params.mod[0].type = kModFollow; r.params.mod[0].cycle = 0.3f; r.params.mod[0].glow = 1.0f;
        loopPlaying (r);
        r.run (silence (rate, 0.3));
        const float live = r.engine.status().modValue[0].load();
        std::printf("  follower: %.3f while the loop plays\n", live);
        CHECK (live > 0.02f, "follower stayed at %.4f while the loop plays", live);
    }
    {   // Pitch target: a square LFO alternates 0 and +1 octave, so the loop's pitch alternates
        Rig r (rate, block);
        r.params.mod[0].type = kModSquare; r.params.mod[0].cycle = 0.05f; r.params.mod[0].glow = 1.0f; r.params.mod[0].target = 4;
        loopPlaying (r);
        int lo = 1 << 30, hi = 0;
        for (int i = 0; i < 40; ++i)
        {
            auto o = r.run (silence (rate, 0.1));
            if (i < 12) continue;
            const int zc = zeroCrossings (o, 0, o.size());
            lo = std::min (lo, zc); hi = std::max (hi, zc);
        }
        std::printf ("  pitch target: zero crossings per 100 ms range %d..%d\n", lo, hi);
        CHECK (hi > 1.6 * lo, "pitch modulation not audible (crossings %d..%d)", lo, hi);
    }
    {   // Crossfade target: square LFO flips between Deck A (playing) and Deck B (empty)
        Rig r (rate, block);
        r.params.mod[0].type = kModSquare; r.params.mod[0].cycle = 0.05f; r.params.mod[0].glow = 1.0f; r.params.mod[0].target = 11;
        loopPlaying (r);
        double lo = 1e9, hi = 0;
        for (int i = 0; i < 40; ++i)
        {
            auto o = r.run (silence (rate, 0.1));
            if (i < 12) continue;
            const double v = rms (o, 0, o.size());
            lo = std::min (lo, v); hi = std::max (hi, v);
        }
        std::printf ("  crossfade target: rms per 100 ms range %.4f..%.4f\n", lo, hi);
        CHECK (hi > 0.05 && lo < hi * 0.2, "crossfade modulation not audible (rms %.4f..%.4f)", lo, hi);
    }
    {   // every type x every target stays finite, in all modes, with both modulators running
        Rig r (rate, block);
        loopPlaying (r, 1);
        for (int type = 0; type < 5; ++type)
            for (int tg = 0; tg < kModTargetCount; ++tg)
            {
                for (int m = 0; m < 2; ++m)
                {
                    r.params.mod[m].type = type; r.params.mod[m].target = (tg + m * 5) % kModTargetCount;
                    r.params.mod[m].glow = 1.0f; r.params.mod[m].cycle = 0.6f; r.params.mod[m].sync = (tg & 1) != 0;
                }
                auto o = r.run (silence (rate, 0.1));
                CHECK (finiteAndBounded (o), "output not finite for modulator type %d target %d", type, tg);
            }
        for (int mode = 0; mode < 3; ++mode)
        {
            r.params.deck[0].mode = mode;
            auto o = r.run (silence (rate, 0.3));
            CHECK (finiteAndBounded (o), "output not finite with modulators in mode %d", mode);
        }
    }
    {   // Trigger sequencer on Deck A (Slice): arm, record three Trigs, stop, play back, clear.
        Rig r (rate, block);
        r.params.tempoBpm = 120.0f;
        r.params.deck[0].mix = 1.0f;
        r.params.deck[0].mode = 1;
        r.run (silence (rate, 0.3));
        r.run (silence (rate, 0.05), recA);
        r.run (tone);
        r.run (silence (rate, 0.05), recA);
        r.run (silence (rate, 0.3));
        const auto& s = r.engine.status().deck[0];
        const uint32_t arm = deckAction (0, kActionSeqArm), trig = deckAction (0, kActionTrig), clear = deckAction (0, kActionSeqClear);
        CHECK (! s.seqHasData.load(), "sequence present before recording");
        r.run (silence (rate, 0.05), arm);
        CHECK (s.seqArmed.load(), "sequencer did not arm");
        r.run (silence (rate, 1.0));          // armed: recording starts on the next launch-quantise tick
        for (int i = 0; i < 3; ++i) { r.run (silence (rate, 0.05), trig); r.run (silence (rate, 0.45)); }
        r.run (silence (rate, 0.05), arm);     // stop
        r.run (silence (rate, 2.5));
        std::printf ("  sequencer: armed=%d recording=%d hasData=%d\n", (int) s.seqArmed.load(), (int) s.seqRecording.load(), (int) s.seqHasData.load());
        CHECK (! s.seqArmed.load() && ! s.seqRecording.load(), "sequencer still armed/recording after stop");
        CHECK (s.seqHasData.load(), "no sequence was stored");
        r.run (silence (rate, 0.05), playA);
        auto o = r.run (silence (rate, 3.0));
        CHECK (finiteAndBounded (o), "sequence playback not finite");
        CHECK (rms (o, 0, o.size()) > 0.01, "sequence playback is silent");
        r.run (silence (rate, 0.05), clear);
        r.run (silence (rate, 0.2));
        CHECK (! s.seqHasData.load(), "sequence still stored after clear");
    }
}


// MIDI notes, transport, fit tempo and the CV-style inputs.
static void testMidiTransportFitCv (double rate, int block)
{
    const uint32_t recA = deckAction (0, kActionRecord), playA = deckAction (0, kActionPlay);
    const auto tone = sine (rate, 1.0, 330.0, 0.5f);
    auto loopPlaying = [&] (Rig& r, int mode = 0)
    {
        r.params.deck[0].mix = 1.0f;
        r.params.deck[0].mode = mode;
        r.run (silence (rate, 0.3));
        r.run (silence (rate, 0.05), recA);
        r.run (tone);
        r.run (silence (rate, 0.05), recA);
        r.run (silence (rate, 0.05), playA);
        r.run (silence (rate, 0.5));
    };
    auto crossingsNow = [&] (Rig& r)
    {
        r.run (silence (rate, 0.3));          // let pitch glides settle
        auto o = r.run (silence (rate, 0.5));
        return zeroCrossings (o, 0, o.size());
    };

    {   // MIDI note: the triggered pass plays at 2^((note - 60) / 12) (later passes of a looping deck play at normal speed,
        // as on the hardware, so measure right after the trigger)
        Rig r (rate, block);
        loopPlaying (r);
        auto window = [&] (Rig& rr)
        {
            rr.run (silence (rate, 0.06));
            auto o = rr.run (silence (rate, 0.3));
            return zeroCrossings (o, 0, o.size());
        };
        const int base = window (r);
        r.noteOn[0] = 72; r.run (silence (rate, 0.001));
        const int up = window (r);
        r.noteOn[0] = 48; r.run (silence (rate, 0.001));
        const int down = window (r);
        r.noteOn[0] = 60; r.run (silence (rate, 0.001));
        const int same = window (r);
        std::printf ("  midi note: crossings per 0.3 s: base %d, note 72 %d, note 48 %d, note 60 %d\n", base, up, down, same);
        CHECK (std::abs (up - 2 * base) < base / 8 + 6, "note 72 is not an octave up (%d vs 2x%d)", up, base);
        CHECK (std::abs (down * 2 - base) < base / 8 + 6, "note 48 is not an octave down (%d vs %d/2)", down, base);
        CHECK (std::abs (same - base) < base / 8 + 6, "note 60 changed the pitch (%d vs %d)", same, base);
        r.noteOn[1] = 127; r.noteOn[0] = 0; r.run (silence (rate, 0.2));   // extremes must stay finite
        CHECK (finiteAndBounded (r.run (silence (rate, 0.3))), "extreme notes not finite");
    }
    {   // transport start / stop
        Rig r (rate, block);
        loopPlaying (r);
        const auto& s = r.engine.status().deck[0];
        CHECK (s.playing.load(), "deck not playing before Stop");
        r.run (silence (rate, 0.05), deckAction (0, kActionTransportStop));
        r.run (silence (rate, 0.1));
        CHECK (! s.playing.load(), "transport Stop did not stop the deck");
        r.run (silence (rate, 0.05), deckAction (0, kActionTransportStart));
        r.run (silence (rate, 0.3));
        CHECK (s.playing.load(), "transport Start did not play the deck");
        Rig e (rate, block);   // an empty deck must not start
        e.run (silence (rate, 0.05), deckAction (0, kActionTransportStart));
        e.run (silence (rate, 0.2));
        CHECK (! e.engine.status().deck[0].playing.load(), "transport Start played an empty deck");
    }
    {   // Fit tempo: a 2.0 s loop over 4 beats is 120 BPM; the engine keeps it until Tempo is touched
        Rig r (rate, block);
        r.params.tempoBpm = 90.0f;
        r.params.hostSync = false;
        r.params.deck[0].mode = 1;
        r.params.deck[0].mix = 1.0f;
        r.run (silence (rate, 0.3));
        r.run (silence (rate, 0.05), recA);
        r.run (sine (rate, 2.0, 330.0, 0.5f));
        r.run (silence (rate, 0.05), recA);
        r.run (silence (rate, 0.2));
        const auto& st = r.engine.status();
        const double seconds = st.deck[0].loopSeconds.load();
        r.params.deck[0].fitBeats = 4;
        r.run (silence (rate, 0.05), deckAction (0, kActionFitTempo));
        r.run (silence (rate, 0.2));
        const double expected = 60.0 * 4 / seconds;
        std::printf ("  fit tempo: loop %.3f s, 4 beats -> %.2f BPM (expected %.2f), engine tempo %.2f\n", seconds, st.fitBpm.load(), expected, st.tempoBpm.load());
        CHECK (st.fitSeq.load() == 1, "fit tempo did not report success");
        CHECK (std::abs (st.fitBpm.load() - expected) < 0.5, "fit tempo %.2f, expected %.2f", st.fitBpm.load(), expected);
        CHECK (std::abs (st.tempoBpm.load() - expected) < 0.5, "engine tempo %.2f after fit, expected %.2f", st.tempoBpm.load(), expected);
        r.params.tempoBpm = 100.0f;
        r.run (silence (rate, 0.2));
        CHECK (std::abs (st.tempoBpm.load() - 100.0f) < 0.5, "Tempo parameter ignored after a fit (%.2f)", st.tempoBpm.load());
        r.params.deck[0].fitBeats = 16;      // 16 beats in about 2 s = ~480 BPM: refused
        r.run (silence (rate, 0.05), deckAction (0, kActionFitTempo));
        r.run (silence (rate, 0.1));
        CHECK (st.fitFailSeq.load() == 1 && st.fitSeq.load() == 1, "out-of-range fit was not refused");
        Rig e (rate, block);
        e.run (silence (rate, 0.05), deckAction (0, kActionFitTempo));
        e.run (silence (rate, 0.1));
        CHECK (e.engine.status().fitFailSeq.load() == 1, "fit on an empty deck was not refused");
    }
    {   // CV-style inputs
        Rig r (rate, block);
        loopPlaying (r);
        const int base = crossingsNow (r);
        r.params.deck[0].cvPitch = 1.0f;
        const int up = crossingsNow (r);
        r.params.deck[0].cvPitch = -1.0f;
        const int down = crossingsNow (r);
        r.params.deck[0].cvPitch = 0.0f;
        std::printf ("  cv pitch: base %d, +1 V %d, -1 V %d crossings per 0.5 s\n", base, up, down);
        CHECK (std::abs (up - 2 * base) < base / 7 + 8, "+1 V/Oct is not an octave up (%d vs 2x%d)", up, base);
        CHECK (std::abs (down * 2 - base) < base / 7 + 8, "-1 V/Oct is not an octave down (%d vs %d/2)", down, base);

        // Mix CV: with Mix at 0 (input only, silent input) the loop is inaudible; +1 brings it in
        r.params.deck[0].mix = 0.0f;
        r.run (silence (rate, 0.4));
        const double quiet = rms (r.run (silence (rate, 0.4)), 0, (size_t) (rate * 0.4));
        r.params.deck[0].cvMix = 1.0f;
        r.run (silence (rate, 0.4));
        const double loud = rms (r.run (silence (rate, 0.4)), 0, (size_t) (rate * 0.4));
        std::printf ("  cv mix: rms %.4f -> %.4f\n", quiet, loud);
        CHECK (quiet < 0.02 && loud > 0.1, "Mix CV not audible (%.4f -> %.4f)", quiet, loud);
        r.params.deck[0].cvMix = 0.0f;
        r.params.deck[0].mix = 1.0f;

        // Crossfade CV: +1 moves to Deck B (empty) so the output falls silent
        r.params.crossfade = 0.0f;
        r.params.cvCrossfade = 1.0f;
        r.run (silence (rate, 0.5));
        const double gone = rms (r.run (silence (rate, 0.4)), 0, (size_t) (rate * 0.4));
        r.params.cvCrossfade = 0.0f;
        r.run (silence (rate, 0.5));
        const double back = rms (r.run (silence (rate, 0.4)), 0, (size_t) (rate * 0.4));
        std::printf ("  cv crossfade: rms %.4f with +1, %.4f with 0\n", gone, back);
        CHECK (gone < 0.02 && back > 0.1, "Crossfade CV not audible (%.4f / %.4f)", gone, back);

        // Position and Size CV: bounded output in every mode, including extremes
        for (int mode = 0; mode < 3; ++mode)
            for (float v : { -1.0f, 1.0f, 0.5f, -0.3f })
            {
                r.params.deck[0].mode = mode;
                r.params.deck[0].envelope = 0.5f;
                r.params.deck[0].cvPosition = v; r.params.deck[0].cvSize = -v;
                auto o = r.run (silence (rate, 0.2));
                CHECK (finiteAndBounded (o), "output not finite with Position/Size CV %.1f in mode %d", v, mode);
            }
        r.params.deck[0].cvPosition = std::nanf ("");   // a bad host value must not hang or poison the engine
        r.params.deck[0].cvPitch = INFINITY;
        CHECK (finiteAndBounded (r.run (silence (rate, 0.2))), "NaN/inf CV not contained");
    }
}

static void testInstancesAreIndependent()
{
    Rig a (44100.0, 512), b (96000.0, 137);
    a.params.deck[0].mix = 1.0f; b.params.deck[0].mix = 1.0f;
    a.run (silence (44100.0, 0.2)); b.run (silence (96000.0, 0.2));
    a.run (silence (44100.0, 0.05), kActionRecord);
    // interleave blocks of work so any shared state would show up
    for (int i = 0; i < 20; ++i) { a.run (sine (44100.0, 0.05, 330.0, 0.5f)); b.run (silence (96000.0, 0.05)); }
    a.run (silence (44100.0, 0.05), kActionPlay);
    auto outA = a.run (silence (44100.0, 1.5));
    auto outB = b.run (silence (96000.0, 1.5));
    const double la = rms (outA, 4410, outA.size()), lb = rms (outB, 9600, outB.size());
    std::printf ("  instances: A rms %.4f, B rms %.6f\n", la, lb);
    CHECK (la > 0.05, "instance A should play its loop (rms %.4f)", la);
    CHECK (lb < 1e-4, "instance B should be silent (rms %.6f): state is shared between instances", lb);
}

static void testModMap()
{
    for (int i = 0; i <= 100; ++i)
    {
        const float n = (float) i / 100.0f;
        CHECK (std::abs (usp::modCurve (n) - spotykach::curved_value (n)) < 1.0e-6f, "modCurve(%.2f) differs from upstream curved_value", n);
    }
    CHECK (usp::modDivisionIndex (0.0f) == 8 && usp::modDivisionIndex (0.5f) == 3 && usp::modDivisionIndex (1.0f) == 0, "division index mapping");
}

static void testPitchMap()
{
    for (int snap = 1; snap <= 2; ++snap)
        for (int i = 0; i <= 1000; ++i)
        {
            const float v = (float) i / 1000.0f;
            const float k = usp::snapKnob (v, snap);
            CHECK (std::abs (usp::snapPitch (k, snap) - usp::snapPitch (v, snap)) < 1.0e-5f, "snap %d: knob %.3f -> %.3f changes the pitch", snap, v, k);
            CHECK (k >= -1e-6f && k <= 1.000001f, "snap %d: knob %.3f out of range", snap, k);
        }
    // +12 semitones and -7 semitones in Semitones mode are exact
    CHECK (std::abs (usp::normToSpeed (usp::snapPitch (0.75f, 2)) - 2.0f) < 1e-4f, "+12 st is not 2x");
    CHECK (std::abs (usp::normToSpeed (usp::snapPitch (0.5f - 7.0f / 48.0f, 2)) - std::pow (2.0f, -7.0f / 12.0f)) < 1e-4f, "-7 st wrong");
}

int main (int argc, char** argv)
{
    testPitchMap();
    testModMap();
    const double rates[] = { 22050.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
    const int blocks[]   = { 1, 64, 137, 512, 4096 };

    const double only = argc > 1 ? std::atof (argv[1]) : 0.0;   // optional: run a single host rate
    for (double rate : rates)
        for (int block : blocks)
        {
            if (only > 0.0 && std::abs (rate - only) > 1.0) continue;
            if (block == 1 && rate > 50000.0) continue;   // keep the run time reasonable
            std::printf ("rate %.0f Hz, block %d\n", rate, block);
            testLatency (rate, block);
            testRecordPlay (rate, block);
            if (block == 64 || block == 512) testDecks (rate, block);
            if (block == 64 || block == 512) testModesTempoPitch (rate, block);
            if (block == 64 || block == 512) testFx (rate, block);
            if (block == 64 || block == 512) testModulatorsAndSequencer (rate, block);
            if (block == 64 || block == 512) testMidiTransportFitCv (rate, block);
        }

    std::printf ("two independent instances\n");
    testInstancesAreIndependent();

#ifdef COUNT_ALLOCS
    std::printf ("heap allocations made inside Engine::process(): %ld\n", gAudioThreadAllocs);
    CHECK (gAudioThreadAllocs == 0, "Engine::process() allocated memory %ld time(s) on the audio thread", gAudioThreadAllocs);
#else
    std::printf ("(allocation counting is off in this build)\n");
#endif

    if (failures != 0) { std::printf ("\n%d FAILURE(S)\n", failures); return 1; }
    std::printf ("\nALL ENGINE TESTS PASSED\n");
    return 0;
}

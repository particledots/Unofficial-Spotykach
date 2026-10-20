// Unofficial Spotykach - own code, GNU AGPLv3 (see LICENSE).
// Headless test of the engine wrapper (no JUCE, no audio device). Run by CI; a failure fails the build.
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
                            params, first ? actionsAtStart : 0u, host);
            if (host.playing) host.ppq += (double) n / rate * host.bpm / 60.0;
            gAudioThreadAllocs += allocCount() - before;
            first = false;
            off += (size_t) n;
        }
        lastOutR = outR;
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

int main (int argc, char** argv)
{
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

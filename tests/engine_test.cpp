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

    Rig (double r, int b) : rate (r), block (b) { engine.prepare (r, b); }

    std::vector<float> run (const std::vector<float>& in, uint32_t actionsAtStart = 0)
    {
        std::vector<float> out (in.size(), 0.0f), outR (in.size(), 0.0f), inR = in;
        std::vector<float> inCopy = in;
        size_t off = 0;
        bool first = true;
        while (off < in.size())
        {
            const int n = (int) std::min ((size_t) block, in.size() - off);
            const long before = allocCount();
            engine.process (inCopy.data() + off, inR.data() + off, out.data() + off, outR.data() + off, n,
                            params, first ? actionsAtStart : 0u);
            gAudioThreadAllocs += allocCount() - before;
            first = false;
            off += (size_t) n;
        }
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
    r.params.mix = 0.0f;   // input only
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
    r.params.mix = 1.0f;       // loop only, so we hear nothing unless the loop plays
    const double recSeconds = 1.0;

    // 0.3 s of silence, arm record, 1.0 s of tone, press Play (ends recording), 3 s of silence
    auto a = silence (rate, 0.3);
    r.run (a);
    r.run (silence (rate, 0.05), kActionRecord);
    r.run (sine (rate, recSeconds, 220.0, 0.5f));
    r.run (silence (rate, 0.05), kActionPlay);
    auto tail = r.run (silence (rate, 3.0));

    const auto& s = r.engine.status();
    std::printf ("  loop: %.3f s, playing=%d, empty=%d, underruns=%u\n", s.loopSeconds.load(), (int) s.playing.load(), (int) s.empty.load(), (unsigned) s.underruns.load());
    CHECK (finiteAndBounded (tail), "output has NaN/inf or is out of range");
    CHECK (! s.empty.load(), "nothing was recorded");
    CHECK (s.playing.load(), "deck is not playing");
    CHECK (std::abs (s.loopSeconds.load() - recSeconds) < 0.15, "loop length %.3f s is not near %.2f s", s.loopSeconds.load(), recSeconds);
    CHECK (s.underruns.load() == 0, "resampler under-ran %u frames", (unsigned) s.underruns.load());
    const double level = rms (tail, (size_t) (rate * 0.5), tail.size());
    CHECK (level > 0.05, "playback is too quiet (rms %.4f)", level);
}

static void testInstancesAreIndependent()
{
    Rig a (44100.0, 512), b (96000.0, 137);
    a.params.mix = 1.0f; b.params.mix = 1.0f;
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

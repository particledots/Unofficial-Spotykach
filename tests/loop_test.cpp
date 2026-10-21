// Unofficial Spotykach - own code, GNU AGPLv3 (see LICENSE).
// Headless test of loop files (WAV in/out, links, search) and of the loop hand-over to the audio thread,
// the deck trims, and the contentSeq counter. No JUCE. Run by CI; a failure fails the build.
#include "Engine.h"
#include "LoopFile.h"
#include "LoopLibrary.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <new>
#include <random>
#include <vector>

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
namespace fs = std::filesystem;

static int failures = 0, checks = 0;
#define CHECK(cond, ...)                                                                       \
    do { ++checks; if (! (cond)) { ++failures; std::printf ("  FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static const double kPi = 3.14159265358979323846;

// ---- WAV file builder (independent of the code under test) ----------------------------------------------
static void put16 (std::vector<uint8_t>& b, uint32_t v) { b.push_back ((uint8_t) v); b.push_back ((uint8_t) (v >> 8)); }
static void put32 (std::vector<uint8_t>& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back ((uint8_t) (v >> (8 * i))); }
static void putId (std::vector<uint8_t>& b, const char* s) { for (int i = 0; i < 4; ++i) b.push_back ((uint8_t) s[i]); }

struct WavSpec
{
    int bits = 16;
    bool isFloat = false;
    int channels = 1;
    uint32_t rate = 48000;
    bool extensible = false;
    std::vector<uint32_t> cues;
    bool cueBeforeData = false;
};

// frames: one double per channel per frame, interleaved, nominal range -1..1
static std::vector<uint8_t> makeWav (const WavSpec& sp, const std::vector<double>& samples)
{
    std::vector<uint8_t> body;
    for (double v : samples)
    {
        v = std::max (-1.0, std::min (1.0, v));
        if (sp.isFloat)
        {
            if (sp.bits == 32) { float f = (float) v; uint32_t u; std::memcpy (&u, &f, 4); put32 (body, u); }
            else { uint64_t u; std::memcpy (&u, &v, 8); put32 (body, (uint32_t) u); put32 (body, (uint32_t) (u >> 32)); }
        }
        else if (sp.bits == 8)  body.push_back ((uint8_t) std::lround (v * 127.0 + 128.0));
        else if (sp.bits == 16) put16 (body, (uint32_t) (uint16_t) (int16_t) std::lround (v * 32767.0));
        else if (sp.bits == 24) { int32_t x = (int32_t) std::lround (v * 8388607.0); body.push_back ((uint8_t) x); body.push_back ((uint8_t) (x >> 8)); body.push_back ((uint8_t) (x >> 16)); }
        else                    put32 (body, (uint32_t) (int32_t) std::lround (v * 2147483647.0));
    }
    std::vector<uint8_t> cue;
    if (! sp.cues.empty())
    {
        put32 (cue, (uint32_t) sp.cues.size());
        for (size_t i = 0; i < sp.cues.size(); ++i)
        {
            put32 (cue, (uint32_t) i + 1); put32 (cue, sp.cues[i]); putId (cue, "data"); put32 (cue, 0); put32 (cue, 0); put32 (cue, sp.cues[i]);
        }
    }
    std::vector<uint8_t> fmt;
    const int tag = sp.isFloat ? 3 : 1;
    put16 (fmt, sp.extensible ? 0xFFFE : (uint32_t) tag);
    put16 (fmt, (uint32_t) sp.channels);
    put32 (fmt, sp.rate);
    put32 (fmt, sp.rate * (uint32_t) (sp.channels * sp.bits / 8));
    put16 (fmt, (uint32_t) (sp.channels * sp.bits / 8));
    put16 (fmt, (uint32_t) sp.bits);
    if (sp.extensible)
    {
        put16 (fmt, 22); put16 (fmt, (uint32_t) sp.bits); put32 (fmt, 0);
        put16 (fmt, (uint32_t) tag);                       // sub-format GUID starts with the format tag
        for (uint8_t g : { 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 }) fmt.push_back (g);
    }
    std::vector<uint8_t> b;
    putId (b, "RIFF"); put32 (b, 0); putId (b, "WAVE");
    putId (b, "fmt "); put32 (b, (uint32_t) fmt.size()); b.insert (b.end(), fmt.begin(), fmt.end());
    if (! cue.empty() && sp.cueBeforeData) { putId (b, "cue "); put32 (b, (uint32_t) cue.size()); b.insert (b.end(), cue.begin(), cue.end()); }
    putId (b, "LIST"); put32 (b, 5); for (int i = 0; i < 5; ++i) b.push_back ('x'); b.push_back (0);   // odd-sized chunk + pad byte
    putId (b, "data"); put32 (b, (uint32_t) body.size()); b.insert (b.end(), body.begin(), body.end());
    if (body.size() & 1) b.push_back (0);
    if (! cue.empty() && ! sp.cueBeforeData) { putId (b, "cue "); put32 (b, (uint32_t) cue.size()); b.insert (b.end(), cue.begin(), cue.end()); }
    const uint32_t riff = (uint32_t) b.size() - 8;
    for (int i = 0; i < 4; ++i) b[4 + (size_t) i] = (uint8_t) (riff >> (8 * i));
    return b;
}

static std::vector<double> tone (double rate, double seconds, double freq, double amp, int channels = 1, double rightAmp = 0.0)
{
    const size_t n = (size_t) std::llround (rate * seconds);
    std::vector<double> v (n * (size_t) channels);
    for (size_t i = 0; i < n; ++i)
    {
        const double s = std::sin (2.0 * kPi * freq * (double) i / rate);
        v[i * (size_t) channels] = amp * s;
        if (channels == 2) v[i * 2 + 1] = rightAmp * s;
    }
    return v;
}

// Largest difference between the decoded audio and the ideal 48 kHz sine, away from the ends.
static double toneError (const std::vector<float>& x, double freq, double amp)
{
    double worst = 0;
    for (size_t i = 200; i + 200 < x.size(); ++i)
        worst = std::max (worst, std::abs ((double) x[i] - amp * std::sin (2.0 * kPi * freq * (double) i / 48000.0)));
    return worst;
}

static int crossings (const std::vector<float>& v, size_t a, size_t b)
{
    int c = 0;
    for (size_t i = std::max<size_t> (a, 1); i < std::min (b, v.size()); ++i) if (v[i - 1] <= 0.0f && v[i] > 0.0f) ++c;
    return c;
}
// Frequency from the median interval between rising zero crossings: robust against the clicks where a loop wraps.
static double medianHz (const std::vector<float>& v, size_t a, size_t b, double rate)
{
    std::vector<double> gaps;
    double last = -1.0;
    for (size_t i = std::max<size_t> (a, 1); i < std::min (b, v.size()); ++i)
        if (v[i - 1] <= 0.0f && v[i] > 0.0f)
        {
            // sub-sample position of the rising crossing
            const double at = (double) (i - 1) + (double) -v[i - 1] / (double) (v[i] - v[i - 1]);
            if (last >= 0.0) gaps.push_back (at - last);
            last = at;
        }
    if (gaps.empty()) return 0.0;
    std::sort (gaps.begin(), gaps.end());
    return rate / gaps[gaps.size() / 2];
}
static double rms (const std::vector<float>& v, size_t a, size_t b)
{
    b = std::min (b, v.size());
    if (b <= a) return 0.0;
    double s = 0;
    for (size_t i = a; i < b; ++i) s += (double) v[i] * v[i];
    return std::sqrt (s / (double) (b - a));
}

// ---- WAV decode / encode ---------------------------------------------------------------------------------
static void testFnv()
{
    std::printf ("fingerprint\n");
    CHECK (fnv1a64 (nullptr, 0) == 14695981039346656037ull, "empty hash");
    const uint8_t a = 'a';
    CHECK (fnv1a64 (&a, 1) == 0xaf63dc4c8601ec8cull, "hash of 'a' is %llx", (unsigned long long) fnv1a64 (&a, 1));
}

static void testDecodeFormats()
{
    std::printf ("decode formats\n");
    struct F { int bits; bool fl; bool ext; double tol; };
    const F formats[] = { { 8, false, false, 0.02 }, { 16, false, false, 0.002 }, { 24, false, false, 0.002 }, { 32, false, false, 0.002 },
                          { 32, true, false, 0.002 }, { 64, true, false, 0.002 }, { 16, false, true, 0.002 }, { 32, true, true, 0.002 } };
    for (const auto& f : formats)
    {
        WavSpec sp; sp.bits = f.bits; sp.isFloat = f.fl; sp.extensible = f.ext; sp.channels = 2; sp.rate = 48000;
        auto wav = makeWav (sp, tone (48000.0, 0.5, 1000.0, 0.5, 2, 0.25));
        LoopAudio a; WavInfo info; std::string msg;
        const auto e = decodeWav (wav.data(), wav.size(), a, info, msg);
        CHECK (e == WavError::none, "%d-bit%s%s refused: %s", f.bits, f.fl ? " float" : "", f.ext ? " ext" : "", msg.c_str());
        if (e != WavError::none) continue;
        CHECK (a.frames() == 24000 && a.r.size() == 24000, "%d-bit frames %zu", f.bits, a.frames());
        CHECK (info.channels == 2 && info.bits == f.bits && info.isFloat == f.fl, "info wrong for %d-bit", f.bits);
        CHECK (toneError (a.l, 1000.0, 0.5) < f.tol, "%d-bit left error %.4f", f.bits, toneError (a.l, 1000.0, 0.5));
        CHECK (toneError (a.r, 1000.0, 0.25) < f.tol, "%d-bit right error %.4f (right is a quarter-scale copy)", f.bits, toneError (a.r, 1000.0, 0.25));
    }
    // Mono is copied to both channels.
    {
        WavSpec sp; sp.bits = 16; sp.channels = 1;
        auto wav = makeWav (sp, tone (48000.0, 0.2, 440.0, 0.4));
        LoopAudio a; WavInfo info; std::string msg;
        CHECK (decodeWav (wav.data(), wav.size(), a, info, msg) == WavError::none, "mono refused");
        CHECK (a.l == a.r && a.frames() == 9600, "mono not duplicated");
    }
}

static void testResample()
{
    std::printf ("resample on load\n");
    for (uint32_t rate : { 44100u, 96000u, 22050u, 32000u, 88200u, 11025u })
    {
        WavSpec sp; sp.bits = 16; sp.channels = 1; sp.rate = rate;
        auto wav = makeWav (sp, tone ((double) rate, 1.0, 1000.0, 0.5));
        LoopAudio a; WavInfo info; std::string msg;
        CHECK (decodeWav (wav.data(), wav.size(), a, info, msg) == WavError::none, "%u Hz refused", rate);
        CHECK (a.frames() == 48000, "%u Hz gave %zu frames, expected 48000", rate, a.frames());
        const double err = toneError (a.l, 1000.0, 0.5);
        std::printf ("  %6u Hz -> 48000: %zu frames, worst error vs ideal sine %.4f\n", rate, a.frames(), err);
        CHECK (err < 0.01, "%u Hz tone is off by %.4f (a one-frame offset would be 0.065)", rate, err);
    }
}

static void testCutAndCues()
{
    std::printf ("42 s limit and cue points\n");
    {
        WavSpec sp; sp.bits = 16; sp.channels = 1; sp.rate = 48000;
        std::vector<double> v ((size_t) 48000 * 50, 0.0);
        v[100] = 0.5;
        auto wav = makeWav (sp, v);
        LoopAudio a; WavInfo info; std::string msg;
        CHECK (decodeWav (wav.data(), wav.size(), a, info, msg) == WavError::none, "50 s file refused");
        CHECK (a.frames() == kMaxLoopFrames && info.cut, "50 s file gave %zu frames, cut=%d", a.frames(), (int) info.cut);
        CHECK (info.fileFrames == (size_t) 48000 * 50, "fileFrames %zu", info.fileFrames);
    }
    {   // 8 s at 8 kHz = 64000 frames at 48 kHz; also exercises cut detection after resampling
        WavSpec sp; sp.bits = 16; sp.channels = 1; sp.rate = 8000;
        auto wav = makeWav (sp, std::vector<double> ((size_t) 8000 * 300, 0.0));
        LoopAudio a; WavInfo info; std::string msg;
        CHECK (decodeWav (wav.data(), wav.size(), a, info, msg) == WavError::none && a.frames() == kMaxLoopFrames && info.cut, "300 s at 8 kHz not cut to 42 s (%zu)", a.frames());
    }
    for (bool before : { false, true })
    {   // cue points at 24 kHz are scaled x2; the one past the end is dropped; order and duplicates are tidied
        WavSpec sp; sp.bits = 16; sp.channels = 1; sp.rate = 24000; sp.cueBeforeData = before;
        sp.cues = { 12000, 0, 24000, 12000, 999999 };
        auto wav = makeWav (sp, tone (24000.0, 1.0, 300.0, 0.3));
        LoopAudio a; WavInfo info; std::string msg;
        CHECK (decodeWav (wav.data(), wav.size(), a, info, msg) == WavError::none, "cue file refused");
        // file frames 24000: cue 24000 is past the last frame and is dropped; 12000 -> 24000 and 0 -> 0 at 48 kHz
        CHECK (a.cues == std::vector<uint32_t> ({ 0, 24000 }), "cues came out as %zu points (first %u)", a.cues.size(), a.cues.empty() ? 0u : a.cues[0]);
    }
    {   // 300 cue points: only 255 are kept (the firmware stores the count in a uint8_t)
        WavSpec sp; sp.bits = 16; sp.channels = 1; sp.rate = 48000;
        for (uint32_t i = 0; i < 300; ++i) sp.cues.push_back (i * 100);
        auto wav = makeWav (sp, tone (48000.0, 1.0, 300.0, 0.3));
        LoopAudio a; WavInfo info; std::string msg;
        CHECK (decodeWav (wav.data(), wav.size(), a, info, msg) == WavError::none && a.cues.size() == kMaxLoopCues, "300 cues gave %zu", a.cues.size());
    }
}

static void testRefusals()
{
    std::printf ("files that must be refused\n");
    LoopAudio a; WavInfo info; std::string msg;
    const uint8_t junk[] = { 'h', 'e', 'l', 'l', 'o' };
    CHECK (decodeWav (junk, sizeof junk, a, info, msg) == WavError::notWav && ! msg.empty(), "junk accepted");
    CHECK (decodeWav (nullptr, 0, a, info, msg) == WavError::notWav, "null accepted");
    WavSpec sp; sp.bits = 16; sp.channels = 1;
    auto good = makeWav (sp, tone (48000.0, 0.1, 440.0, 0.3));
    {   // 3 channels
        auto w = good; w[22] = 3;
        CHECK (decodeWav (w.data(), w.size(), a, info, msg) == WavError::unsupported, "3 channels accepted");
    }
    {   // format tag 2 (ADPCM)
        auto w = good; w[20] = 2;
        CHECK (decodeWav (w.data(), w.size(), a, info, msg) == WavError::unsupported, "ADPCM accepted");
    }
    {   // 12 bits
        auto w = good; w[34] = 12;
        CHECK (decodeWav (w.data(), w.size(), a, info, msg) == WavError::unsupported, "12-bit accepted");
    }
    {   // no audio
        WavSpec s2; s2.bits = 16; s2.channels = 1;
        auto w = makeWav (s2, {});
        CHECK (decodeWav (w.data(), w.size(), a, info, msg) == WavError::empty, "empty data accepted");
    }
    {   // a file cut off in the middle of the audio decodes what is there
        auto w = good; w.resize (w.size() - 1000);
        CHECK (decodeWav (w.data(), w.size(), a, info, msg) == WavError::none && a.frames() > 0 && a.frames() < 4800, "truncated file gave %zu frames", a.frames());
    }
}

static void testFuzz()
{
    std::printf ("damaged files (must never crash)\n");
    WavSpec sp; sp.bits = 24; sp.channels = 2; sp.rate = 44100; sp.cues = { 10, 500, 700 };
    const auto good = makeWav (sp, tone (44100.0, 0.1, 440.0, 0.3, 2, 0.2));
    std::mt19937 rng (12345);
    int decoded = 0;
    for (int i = 0; i < 400; ++i)
    {
        auto w = good;
        const int kind = i % 4;
        if (kind == 0) { w.resize (rng() % w.size()); }
        else if (kind == 1) { for (int k = 0; k < 8; ++k) w[rng() % w.size()] = (uint8_t) rng(); }
        else if (kind == 2) { for (int k = 0; k < 4; ++k) w[rng() % 80] = (uint8_t) rng(); }                       // damage the headers
        else { for (int k = 0; k < 4; ++k) { size_t at = rng() % (w.size() - 4); uint32_t v = (uint32_t) rng(); std::memcpy (&w[at], &v, 4); } }
        LoopAudio a; WavInfo info; std::string msg;
        const auto e = decodeWav (w.empty() ? nullptr : w.data(), w.size(), a, info, msg);
        if (e == WavError::none)
        {
            ++decoded;
            CHECK (a.l.size() == a.r.size() && a.frames() <= kMaxLoopFrames, "decoded sizes inconsistent");
            for (uint32_t c : a.cues) CHECK (c < a.frames(), "cue %u outside the loop", c);
            for (float v : a.l) if (! std::isfinite (v)) { CHECK (false, "non-finite sample"); break; }
        }
    }
    std::printf ("  400 damaged variants handled (%d still decoded)\n", decoded);
}

static void testEncodeRoundTrip()
{
    std::printf ("encode then decode\n");
    LoopAudio a;
    const size_t n = 30000;
    a.l.resize (n); a.r.resize (n);
    std::mt19937 rng (7);
    for (size_t i = 0; i < n; ++i) { a.l[i] = ((float) (rng() % 20001) - 10000.0f) / 10000.0f; a.r[i] = ((float) (rng() % 20001) - 10000.0f) / 10000.0f; }
    a.cues = { 0, 7500, 15000, 22500 };
    std::vector<uint8_t> bytes;
    encodeWav (a, bytes);
    // The firmware's header: IEEE float, 2 channels, 48000 Hz, 32 bit.
    CHECK (std::memcmp (bytes.data(), "RIFF", 4) == 0 && std::memcmp (bytes.data() + 8, "WAVEfmt ", 8) == 0, "header ids");
    CHECK (bytes[20] == 3 && bytes[22] == 2 && bytes[34] == 32, "format is not float32 stereo");
    LoopAudio b; WavInfo info; std::string msg;
    CHECK (decodeWav (bytes.data(), bytes.size(), b, info, msg) == WavError::none, "own file refused: %s", msg.c_str());
    CHECK (a.l == b.l && a.r == b.r, "samples differ after the round trip");
    CHECK (a.cues == b.cues, "cue points differ after the round trip");
    // Without cue points no cue chunk is written.
    a.cues.clear();
    encodeWav (a, bytes);
    CHECK (std::search (bytes.begin(), bytes.end(), "cue ", "cue " + 4) == bytes.end(), "a cue chunk was written for a loop without slice points");
}

// ---- Library: files, links, search -----------------------------------------------------------------------
static void testLibrary()
{
    std::printf ("files, links and search\n");
    char tmpl[] = "/tmp/usp_loop_test_XXXXXX";
    const char* dirName = mkdtemp (tmpl);
    CHECK (dirName != nullptr, "no temp folder");
    if (dirName == nullptr) return;
    const fs::path root (dirName);
    const fs::path home = root / "home";
    fs::create_directories (home / "sub" / "deeper");

    auto mk = [] (double freq, double seconds) {
        WavSpec sp; sp.bits = 16; sp.channels = 1;
        return makeWav (sp, tone (48000.0, seconds, freq, 0.3));
    };
    auto original = mk (440.0, 0.5);
    auto decoy = mk (441.0, 0.5);                           // same size, different audio
    CHECK (original.size() == decoy.size(), "decoy has to be the same size");

    const std::string stored = (home / "a.wav").string();
    CHECK (writeFileBytes (stored, original), "write failed");
    CHECK (! fs::exists (stored + ".part"), "temporary file left behind");
    std::vector<uint8_t> back;
    CHECK (readFileBytes (stored, back) && back == original, "read back differs");
    CHECK (! writeFileBytes ("/proc/definitely/not/here.wav", original), "write to an impossible place succeeded");
    CHECK (! readFileBytes ((home / "nope.wav").string(), back), "read of a missing file succeeded");
    CHECK (! readFileBytes (stored, back, 10), "size limit ignored");

    auto link = makeLink (stored, home.string(), original);
    CHECK (link.rel == "a.wav" && link.size == original.size() && link.fingerprint == fnv1a64 (original.data(), original.size()), "link fields wrong (rel '%s')", link.rel.c_str());
    CHECK (makeLink ((root / "elsewhere.wav").string(), home.string(), original).rel.empty(), "a file outside the home folder got a relative path");

    {   auto r = resolveLink (link, home.string());
        CHECK (r.state == LinkState::found && r.path == stored && r.bytes == original, "unchanged file not found"); }

    // Renamed and moved into a sub-folder: found by fingerprint. A same-size decoy with the ORIGINAL name sits
    // at the stored path... that would be "changed", so first test with the decoy elsewhere.
    fs::rename (stored, home / "sub" / "deeper" / "particle.wav");
    CHECK (writeFileBytes ((home / "sub" / "decoy.wav").string(), decoy), "decoy write");
    CHECK (writeFileBytes ((home / "sub" / "other.WAV").string(), mk (300.0, 0.2)), "other write");
    {   auto r = resolveLink (link, home.string());
        CHECK (r.state == LinkState::moved && fs::path (r.path).filename() == "particle.wav" && r.bytes == original,
               "moved file not found by fingerprint (state %d, '%s')", (int) r.state, r.path.c_str()); }

    // The decoy alone is never taken for the original.
    fs::remove (home / "sub" / "deeper" / "particle.wav");
    {   auto r = resolveLink (link, home.string());
        CHECK (r.state == LinkState::missing && r.bytes.empty(), "a same-size decoy was accepted (state %d)", (int) r.state); }

    // A file at the stored path whose audio differs: loaded, but reported as changed.
    CHECK (writeFileBytes (stored, decoy), "write changed file");
    {   auto r = resolveLink (link, home.string());
        CHECK (r.state == LinkState::changed && r.path == stored && r.bytes == decoy, "changed file not reported (state %d)", (int) r.state); }
    fs::remove (stored);

    // The project opened on another computer: different home folder, same relative place.
    const fs::path home2 = root / "other_home";
    fs::create_directories (home2);
    CHECK (writeFileBytes ((home2 / "a.wav").string(), original), "write second home");
    {   auto r = resolveLink (link, home2.string());
        CHECK (r.state == LinkState::found && r.path == (home2 / "a.wav").string(), "relative path not used (state %d, '%s')", (int) r.state, r.path.c_str()); }

    // Nothing anywhere.
    fs::remove (home2 / "a.wav");
    {   auto r = resolveLink (link, home2.string());
        CHECK (r.state == LinkState::missing, "expected missing"); }
    {   LoopLink none;
        CHECK (resolveLink (none, home.string()).state == LinkState::missing, "empty link not missing"); }
    {   auto r = resolveLink (link, "");     // no home folder at all: only the stored path counts
        CHECK (r.state == LinkState::missing, "expected missing with no home folder"); }

    fs::remove_all (root);
}

// ---- Engine: loop hand-over -------------------------------------------------------------------------------
struct Rig
{
    Engine engine;
    double rate;
    int block;
    EngineParams params;
    double maxBlockMs = 0.0;

    Rig (double r, int b = 512) : rate (r), block (b) { params.crossfade = 0.0f; engine.prepare (r, b); }

    std::vector<float> run (const std::vector<float>& in, uint32_t actions = 0)
    {
        std::vector<float> out (in.size(), 0.0f), outR (in.size(), 0.0f), inR = in, inCopy = in;
        size_t off = 0;
        bool first = true;
        while (off < in.size())
        {
            const int n = (int) std::min ((size_t) block, in.size() - off);
            const long before = allocCount();
            const auto t0 = std::chrono::steady_clock::now();
            engine.process (inCopy.data() + off, inR.data() + off, out.data() + off, outR.data() + off, n, params, first ? actions : 0u);
            const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
            maxBlockMs = std::max (maxBlockMs, ms);
            gAudioThreadAllocs += allocCount() - before;
            first = false;
            off += (size_t) n;
        }
        return out;
    }
    std::vector<float> silence (double seconds) { return run (std::vector<float> ((size_t) (rate * seconds), 0.0f)); }
    std::vector<float> sine (double seconds, double freq, float amp, uint32_t actions = 0)
    {
        std::vector<float> v ((size_t) (rate * seconds));
        for (size_t i = 0; i < v.size(); ++i) v[i] = amp * (float) std::sin (2.0 * kPi * freq * (double) i / rate);
        return run (v, actions);
    }

    // Runs silence until the audio thread hands the transfer back (or gives up).
    std::unique_ptr<LoopTransfer> waitLoad (int deck, double maxSeconds = 3.0)
    {
        for (double t = 0; t < maxSeconds; t += 0.02)
        {
            silence (0.02);
            if (auto r = engine.collectLoad (deck)) return r;
        }
        return nullptr;
    }
    std::unique_ptr<LoopTransfer> waitSave (int deck, double maxSeconds = 3.0)
    {
        for (double t = 0; t < maxSeconds; t += 0.02)
        {
            silence (0.02);
            if (auto r = engine.collectSave (deck)) return r;
        }
        return nullptr;
    }

    // Record `seconds` of a tone into deck d: arm, tone, then Play (which ends the recording).
    void record (int d, double seconds, double freq, float amp)
    {
        silence (0.1);
        run (std::vector<float> ((size_t) (rate * 0.05), 0.0f), deckAction (d, kActionRecord));
        sine (seconds, freq, amp);
        run (std::vector<float> ((size_t) (rate * 0.05), 0.0f), deckAction (d, kActionPlay));
        silence (0.3);
    }

    std::unique_ptr<LoopTransfer> save (int d)
    {
        auto t = std::make_unique<LoopTransfer>();
        const uint32_t n = engine.status().deck[d].loopFrames.load();
        t->audio.l.assign (n, 0.0f);
        t->audio.r.assign (n, 0.0f);
        t->cues.assign (kMaxLoopCues, 0u);
        if (! engine.submitSave (d, t)) return nullptr;
        return waitSave (d);
    }
    std::unique_ptr<LoopTransfer> load (int d, LoopAudio audio)
    {
        auto t = std::make_unique<LoopTransfer>();
        t->audio = std::move (audio);
        if (! engine.submitLoad (d, t)) return nullptr;
        return waitLoad (d);
    }
};

static void testTransfers (double rate)
{
    std::printf ("loop hand-over at host rate %.0f Hz\n", rate);
    Rig r (rate);
    r.params.deck[0].mix = 1.0f;
    r.params.deck[1].mix = 1.0f;
    const auto& st = r.engine.status();

    // 1. Record a 1 s tone in deck A and save it.
    r.record (0, 1.0, 220.0, 0.5f);
    CHECK (! st.deck[0].empty.load(), "deck A empty after recording");
    const uint32_t seqAfterRecord = st.deck[0].contentSeq.load();
    CHECK (seqAfterRecord >= 1, "contentSeq did not move when recording started");
    auto saved = r.save (0);
    CHECK (saved != nullptr && saved->result == LoopTransfer::done, "save did not complete (result %d)", saved ? saved->result : -1);
    if (! saved || saved->result != LoopTransfer::done) return;
    const size_t n = saved->audio.frames();
    std::printf ("  saved %zu frames (%.3f s), contentSeq %u\n", n, (double) n / 48000.0, (unsigned) saved->contentSeq);
    CHECK (std::abs ((double) n / 48000.0 - 1.0) < 0.15, "saved loop is %.3f s", (double) n / 48000.0);
    CHECK (saved->contentSeq == seqAfterRecord, "save reported seq %u, expected %u", (unsigned) saved->contentSeq, (unsigned) seqAfterRecord);
    CHECK (saved->cueCount == 0, "a plain loop reported %zu slice points", saved->cueCount);
    const double level = rms (saved->audio.l, n / 4, n * 3 / 4);
    CHECK (std::abs (level - 0.5 / std::sqrt (2.0)) < 0.06, "saved level %.3f, expected 0.354", level);
    const int cr = crossings (saved->audio.l, n / 4, n * 3 / 4);
    const double hz = cr / (0.5 * (double) n / 48000.0);
    CHECK (std::abs (hz - 220.0) < 12.0, "saved tone is %.1f Hz", hz);
    CHECK (st.deck[0].contentSeq.load() == seqAfterRecord, "contentSeq moved without a change to the loop");

    // 2. Load it into deck B, play B, hear the tone; save B and compare bit for bit.
    LoopAudio copy = saved->audio;
    auto loaded = r.load (1, copy);
    CHECK (loaded != nullptr && loaded->result == LoopTransfer::done, "load did not complete");
    if (loaded == nullptr) return;
    CHECK (! st.deck[1].empty.load() && st.deck[1].loopFrames.load() == n, "deck B holds %u frames, expected %zu", (unsigned) st.deck[1].loopFrames.load(), n);
    CHECK (loaded->contentSeq == st.deck[1].contentSeq.load(), "load seq %u vs status %u", (unsigned) loaded->contentSeq, (unsigned) st.deck[1].contentSeq.load());
    r.params.crossfade = 1.0f;                                    // deck B only
    r.silence (0.1);
    r.run (std::vector<float> ((size_t) (rate * 0.05), 0.0f), deckAction (1, kActionPlay));
    r.silence (0.2);
    auto heard = r.silence (1.5);
    const double hl = rms (heard, (size_t) (rate * 0.3), heard.size());
    const double hhz = medianHz (heard, (size_t) (rate * 0.3), heard.size(), rate);
    std::printf ("  deck B plays the loaded loop: rms %.3f, %.1f Hz\n", hl, hhz);
    CHECK (hl > 0.1, "deck B is silent after the load (rms %.4f)", hl);
    CHECK (std::abs (hhz - 220.0) < 3.0, "loaded loop plays at %.1f Hz instead of 220", hhz);
    auto savedB = r.save (1);
    CHECK (savedB != nullptr && savedB->result == LoopTransfer::done, "save of deck B failed");
    if (savedB) CHECK (savedB->audio.l == copy.l && savedB->audio.r == copy.r, "audio differs after load then save");
    const uint32_t seqB = st.deck[1].contentSeq.load();
    r.silence (0.5);
    CHECK (st.deck[1].contentSeq.load() == seqB, "contentSeq changed while only playing");

    // 3. Recording again changes contentSeq (so the project knows the loop no longer matches its file).
    r.params.crossfade = 0.0f;
    r.run (std::vector<float> ((size_t) (rate * 0.05), 0.0f), deckAction (0, kActionRecord));
    CHECK (st.deck[0].contentSeq.load() > seqAfterRecord, "contentSeq did not move when a new recording was armed");

    // 4. A transfer is refused while the deck is being recorded.
    {
        auto tl = std::make_unique<LoopTransfer>();
        tl->audio = copy;
        CHECK (r.engine.submitLoad (0, tl), "submit refused");
        auto res = r.waitLoad (0);
        CHECK (res && res->result == LoopTransfer::refused, "load into a recording deck was not refused (result %d)", res ? res->result : -1);
        auto ts = std::make_unique<LoopTransfer>();
        const uint32_t nowFrames = st.deck[0].loopFrames.load();   // right size, so only the recording state can refuse it
        CHECK (nowFrames > 0 && st.deck[0].recording.load(), "expected an overdub in progress (frames %u)", (unsigned) nowFrames);
        ts->audio.l.assign (nowFrames, 0.0f); ts->audio.r.assign (nowFrames, 0.0f); ts->cues.assign (kMaxLoopCues, 0u);
        CHECK (r.engine.submitSave (0, ts), "submit refused");
        auto res2 = r.waitSave (0);
        CHECK (res2 && res2->result == LoopTransfer::refused, "save of a recording deck was not refused (result %d)", res2 ? res2->result : -1);
    }
    r.run (std::vector<float> ((size_t) (rate * 0.05), 0.0f), deckAction (0, kActionRecord));   // stop the recording

    // 5. Only one transfer per deck and direction at a time; wrong sizes are refused; empty audio is refused.
    {
        auto a = std::make_unique<LoopTransfer>(); a->audio = copy;
        auto b = std::make_unique<LoopTransfer>(); b->audio = copy;
        CHECK (r.engine.submitLoad (1, a), "first submit refused");
        CHECK (! r.engine.submitLoad (1, b) && b != nullptr, "second submit accepted while one is outstanding");
        CHECK (r.waitLoad (1) != nullptr, "first load did not finish");
        auto e = std::make_unique<LoopTransfer>();
        CHECK (r.engine.submitLoad (1, e), "submit of empty audio refused at submit");
        auto er = r.waitLoad (1);
        CHECK (er && er->result == LoopTransfer::refused, "empty audio was not refused");
        auto bad = std::make_unique<LoopTransfer>();
        bad->audio.l.assign (100, 0.0f); bad->audio.r.assign (100, 0.0f); bad->cues.assign (kMaxLoopCues, 0u);
        CHECK (r.engine.submitSave (1, bad), "submit refused");
        auto br = r.waitSave (1);
        CHECK (br && br->result == LoopTransfer::refused, "a save sized wrongly was not refused");
    }
}

static void testCuesAndSlice (double rate)
{
    std::printf ("slice points through the deck at %.0f Hz\n", rate);
    Rig r (rate);
    r.params.crossfade = 1.0f;
    r.params.deck[1].mode = 1;               // Slice
    r.params.deck[1].mix = 1.0f;
    r.silence (0.2);                         // let the mode take effect before loading
    LoopAudio a;
    const size_t n = 96000;                  // 2 s
    a.l.resize (n); a.r.resize (n);
    for (size_t i = 0; i < n; ++i) a.l[i] = a.r[i] = 0.4f * (float) std::sin (2.0 * kPi * 330.0 * (double) i / 48000.0);
    a.cues = { 0, 24000, 48000, 72000 };
    auto res = r.load (1, a);
    CHECK (res && res->result == LoopTransfer::done, "load into a Slice deck failed");
    auto back = r.save (1);
    CHECK (back && back->result == LoopTransfer::done, "save from a Slice deck failed");
    if (back)
    {
        std::vector<uint32_t> got (back->cues.begin(), back->cues.begin() + (long) back->cueCount);
        CHECK (got == a.cues, "slice points came back as %zu points", got.size());
        CHECK (back->audio.l == a.l, "audio changed in a Slice deck");
    }
    const double secs = r.engine.status().deck[1].loopSeconds.load();
    CHECK (std::abs (secs - 2.0) < 1e-3, "loop length %.4f s", secs);
    // Slice mode plays it (the grid exists and the deck is not stuck).
    r.run (std::vector<float> ((size_t) (rate * 0.05), 0.0f), deckAction (1, kActionPlay));
    auto out = r.silence (2.0);
    CHECK (rms (out, (size_t) (rate * 0.5), out.size()) > 0.02, "a loaded loop is silent in Slice mode");
}

static void testBigLoad (double rate, int block)
{
    Rig r (rate, block);
    r.params.crossfade = 1.0f;
    r.params.deck[1].mix = 1.0f;
    LoopAudio a;
    a.l.assign (kMaxLoopFrames, 0.0f); a.r.assign (kMaxLoopFrames, 0.0f);
    for (size_t i = 0; i < kMaxLoopFrames; ++i) a.l[i] = a.r[i] = 0.3f * (float) std::sin (2.0 * kPi * 200.0 * (double) i / 48000.0);
    r.silence (0.3);
    r.maxBlockMs = 0;
    auto t = std::make_unique<LoopTransfer>();
    t->audio = std::move (a);
    CHECK (r.engine.submitLoad (1, t), "submit refused");
    double waited = 0;
    std::unique_ptr<LoopTransfer> res;
    while (waited < 5.0 && ! (res = r.engine.collectLoad (1))) { r.silence (0.01); waited += 0.01; }
    CHECK (res && res->result == LoopTransfer::done, "42 s load did not complete");
    std::printf ("  42 s load at %.0f Hz, block %d: done after %.2f s of audio, slowest process() call %.3f ms\n", rate, block, waited, r.maxBlockMs);
    CHECK (r.engine.status().deck[1].loopFrames.load() == kMaxLoopFrames, "loop is %u frames", (unsigned) r.engine.status().deck[1].loopFrames.load());
    CHECK (r.maxBlockMs < 25.0, "a block took %.2f ms during the load", r.maxBlockMs);   // generous: this box is not a Mac and may be busy
    CHECK (waited < 1.0, "the load took %.2f s of audio", waited);
}

// Trims: input trim sets what is recorded; output trim sets what is heard; changes glide.
static void testTrims (double rate)
{
    std::printf ("trims at %.0f Hz\n", rate);
    auto recordedLevel = [&] (float inTrim)
    {
        Rig r (rate);
        r.params.deck[0].mix = 1.0f;
        r.params.deck[0].inTrim = inTrim;
        r.record (0, 1.0, 220.0, 0.5f);
        auto s = r.save (0);
        return s ? rms (s->audio.l, s->audio.frames() / 4, s->audio.frames() * 3 / 4) : -1.0;
    };
    const double full = recordedLevel (1.0f), half = recordedLevel (0.5f);
    std::printf ("  recorded level: trim 1.0 -> %.3f, trim 0.5 -> %.3f\n", full, half);
    CHECK (full > 0.3 && std::abs (half / full - 0.5) < 0.03, "input trim 0.5 recorded at %.3f of full", half / std::max (full, 1e-9));

    Rig r (rate);
    r.params.deck[0].mix = 1.0f;
    r.record (0, 1.0, 220.0, 0.1f);       // quiet, so the core's output soft limiter does not bend the ratio
    auto at = [&] (float outTrim) {
        r.params.deck[0].outTrim = outTrim;
        r.silence (0.3);                                      // settle
        auto v = r.silence (1.0);
        return rms (v, (size_t) (rate * 0.2), v.size());
    };
    const double o1 = at (1.0f), o2 = at (0.5f), o0 = at (0.0f);
    std::printf ("  heard level: trim 1.0 -> %.3f, 0.5 -> %.3f, 0.0 -> %.4f\n", o1, o2, o0);
    CHECK (o1 > 0.04, "deck is quiet at unity (%.3f)", o1);
    CHECK (std::abs (o2 / o1 - 0.5) < 0.03, "output trim 0.5 gave %.3f of full", o2 / o1);
    CHECK (o0 < 0.002, "output trim 0 left %.4f", o0);

    // A step from unity to zero must glide: no sample-to-sample jump larger than the music itself makes.
    r.params.deck[0].outTrim = 1.0f;
    r.silence (0.5);
    auto steady = r.silence (0.3);
    double maxStep = 0;
    for (size_t i = 1; i < steady.size(); ++i) maxStep = std::max (maxStep, (double) std::abs (steady[i] - steady[i - 1]));
    r.params.deck[0].outTrim = 0.0f;
    auto fade = r.silence (0.3);
    double fadeStep = 0;
    for (size_t i = 1; i < fade.size(); ++i) fadeStep = std::max (fadeStep, (double) std::abs (fade[i] - fade[i - 1]));
    std::printf ("  largest sample step: steady %.4f, during the fade %.4f\n", maxStep, fadeStep);
    CHECK (fadeStep < maxStep * 1.25 + 0.002, "trim change jumps (step %.4f against %.4f)", fadeStep, maxStep);
}

int main()
{
    testFnv();
    testDecodeFormats();
    testResample();
    testCutAndCues();
    testRefusals();
    testFuzz();
    testEncodeRoundTrip();
    testLibrary();
    for (double rate : { 48000.0, 44100.0 }) testTransfers (rate);
    testCuesAndSlice (48000.0);
    testCuesAndSlice (96000.0);
    testBigLoad (48000.0, 128);
    testBigLoad (44100.0, 512);
    for (double rate : { 48000.0, 44100.0 }) testTrims (rate);

    std::printf ("heap allocations made inside Engine::process(): %ld\n", gAudioThreadAllocs);
#ifdef COUNT_ALLOCS
    CHECK (gAudioThreadAllocs == 0, "audio thread allocated %ld times", gAudioThreadAllocs);
#endif
    std::printf ("%d checks, %d failed\n", checks, failures);
    if (failures == 0) std::printf ("ALL LOOP TESTS PASSED\n");
    return failures == 0 ? 0 : 1;
}

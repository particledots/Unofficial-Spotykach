// Unofficial Spotykach - own code, GNU AGPLv3 (see LICENSE).
// Not an official Synthux Academy product; not affiliated with Synthux Academy.
//
// Streaming stereo windowed-sinc resampler for the audio thread.
// - prepare() allocates everything (call it off the audio thread).
// - push() / pull() / available() never allocate, lock or touch the disk.
// - Linear interpolation between neighbouring filter phases, per-phase DC normalisation.
//
// The table-building idea (Blackman-windowed sinc, per-phase normalisation) follows the
// resampler in the author's own Unofficial Beads plug-in; this version replaces its
// std::vector push/erase with fixed buffers so nothing allocates while audio runs.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

class StreamResampler
{
public:
    static constexpr int kTaps = 32;       // filter width in input samples
    static constexpr int kPhases = 512;    // fractional-position resolution (interpolated)

    // Converts from inRate to outRate. maxPush = most input frames ever pushed in one call.
    void prepare (double inRate, double outRate, int maxPush)
    {
        bypassed = std::abs (inRate - outRate) < 0.5;
        step = inRate / outRate;
        const double cutoff = std::min (1.0, outRate / inRate) * 0.97;

        table.assign ((size_t) (kPhases + 1) * (size_t) kTaps, 0.0f);
        const double pi = 3.14159265358979323846;
        for (int p = 0; p <= kPhases; ++p)
        {
            const double frac = (double) p / (double) kPhases;
            double sum = 0.0;
            float* row = &table[(size_t) p * (size_t) kTaps];
            for (int k = 0; k < kTaps; ++k)
            {
                const double d = (double) (k - (kTaps / 2 - 1)) - frac;
                const double x = d * cutoff;
                const double s = std::abs (x) < 1e-9 ? 1.0 : std::sin (pi * x) / (pi * x);
                const double w = 0.42 + 0.5 * std::cos (pi * d / (kTaps / 2)) + 0.08 * std::cos (2.0 * pi * d / (kTaps / 2));
                const double h = s * w * cutoff;
                row[k] = (float) h;
                sum += h;
            }
            for (int k = 0; k < kTaps; ++k) row[k] = (float) ((double) row[k] / sum);
        }

        capacity = (size_t) maxPush + (size_t) (4 * kTaps) + 64 + (size_t) std::ceil (step) * 2;
        for (auto& h : hist) h.assign (capacity, 0.0f);
        reset (0);
    }

    // Empties the history. primeZeros extra silent frames are placed before the stream, which
    // delays it by that many input frames but guarantees later pulls are never short.
    void reset (int primeZeros)
    {
        for (auto& h : hist) std::fill (h.begin(), h.end(), 0.0f);
        count = (size_t) std::max (0, primeZeros);
        pos = 0.0;
    }

    bool isBypass() const { return bypassed; }

    void push (const float* l, const float* r, int n)
    {
        if (bypassed || n <= 0) return;
        compact();
        const size_t room = capacity - count;
        const size_t take = std::min ((size_t) n, room);   // never overruns; excess is dropped
        std::memcpy (&hist[0][count], l, take * sizeof (float));
        std::memcpy (&hist[1][count], r, take * sizeof (float));
        count += take;
        dropped += (size_t) n - take;
    }

    // Number of output frames that can be produced right now.
    int available() const
    {
        if (bypassed) return 0;
        const double last = (double) count - (double) kTaps;     // need floor(pos) + kTaps <= count
        if (pos > last) return 0;
        return (int) std::floor ((last - pos) / step) + 1;
    }

    // Produces up to nOut frames; returns the number written.
    int pull (float* l, float* r, int nOut)
    {
        int produced = 0;
        while (produced < nOut)
        {
            const size_t i = (size_t) pos;
            if (i + (size_t) kTaps > count) break;
            const double frac = pos - (double) i;
            const double fp = frac * (double) kPhases;
            int p = (int) fp;
            if (p >= kPhases) p = kPhases - 1;
            const float w = (float) (fp - (double) p);
            const float* h0 = &table[(size_t) p * (size_t) kTaps];
            const float* h1 = h0 + kTaps;
            const float* xl = &hist[0][i];
            const float* xr = &hist[1][i];
            float al0 = 0.f, al1 = 0.f, ar0 = 0.f, ar1 = 0.f;
            for (int k = 0; k < kTaps; ++k)
            {
                al0 += h0[k] * xl[k];
                al1 += h1[k] * xl[k];
                ar0 += h0[k] * xr[k];
                ar1 += h1[k] * xr[k];
            }
            l[produced] = al0 + w * (al1 - al0);
            r[produced] = ar0 + w * (ar1 - ar0);
            pos += step;
            ++produced;
        }
        return produced;
    }

    size_t droppedFrames() const { return dropped; }

private:
    void compact()
    {
        const size_t drop = (size_t) pos;
        if (drop == 0) return;
        const size_t keep = count > drop ? count - drop : 0;
        for (auto& h : hist) std::memmove (&h[0], &h[drop], keep * sizeof (float));
        count = keep;
        pos -= (double) drop;
    }

    std::vector<float> table;
    std::vector<float> hist[2];
    size_t capacity = 0, count = 0, dropped = 0;
    double step = 1.0, pos = 0.0;
    bool bypassed = true;
};

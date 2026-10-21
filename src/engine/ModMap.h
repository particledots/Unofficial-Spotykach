// Unofficial Spotykach - own code, GNU AGPLv3 (see LICENSE).
// Display helpers for the modulator controls: the same maths the firmware's Modulator/LFO/Follower use
// (third_party/spotykach/src/core/lfo.h curved_value(), modulator.cpp, follower.cpp, config.h), restated
// here so the window can show real units. tests/engine_test.cpp checks them against the upstream code.
#pragma once
#include <algorithm>

namespace usp
{
    // Same curve as upstream curved_value().
    inline float modCurve (float norm)
    {
        norm = norm < 0.5f ? 1.5f * norm / (norm + 1.0f) : norm * norm * 0.6666666667f + 0.334f;
        return std::min (1.0f, std::max (0.0f, norm));
    }

    // Free-running LFO rate in Hz: speed * kLFOFreqRange + kLFOFreqMin (config.h: 11.99 and 0.01).
    inline float modRateHz (float cycle) { return modCurve (cycle) * 11.99f + 0.01f; }

    // Tempo-synced LFO: index into the firmware's division table {0.5, 1, 2, 4, 8, 16, 32, 48, 64} sixteenths.
    // The engine keeps cycle just below 1.0 when synced (upstream would index -1 at exactly 1.0).
    inline int modDivisionIndex (float cycle)
    {
        const float c = std::min (cycle, 0.999f);
        return std::min (8, std::max (0, (int) ((1.0f - std::min (1.0f, std::max (0.0f, c))) * 9.0f - 1.0f)));
    }
    inline const char* modDivisionName (int index)
    {
        static const char* names[9] = { "1/32", "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "3 bars", "4 bars" };
        return names[std::min (8, std::max (0, index))];
    }

    // Follower release time in ms (follower.h: kMinFadeMs 8 + speed * kReleaseRangeMs 3992).
    inline float modFollowReleaseMs (float cycle) { return 8.0f + modCurve (cycle) * 3992.0f; }
}

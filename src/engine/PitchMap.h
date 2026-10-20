// Unofficial Spotykach - own code, GNU AGPLv3 (see LICENSE).
// Pitch mapping shared by the engine and the user interface so what the window shows is what the engine plays.
// The step table and the 0..1 -> speed mapping come from the Spotykach firmware (MIT): src/ui/core.ui.h
// (kSpeedSteps) and src/core/generator.cpp (mapped_speed).
#pragma once
#include <algorithm>
#include <cmath>

namespace usp
{
    // Firmware pitch-snap steps as normalised pitch values: -24, -12, -7, 0, +7, +12, +24 semitones.
    constexpr float kSpeedSteps[7] = { 0.125f, 0.25f, 0.33371f, 0.5f, 0.5830516667f, 0.666666667f, 1.0f };

    // Normalised pitch (0..1) to speed multiplier: 0.5 = 1x, 1.0 = 4x, 0 = stopped.
    inline float normToSpeed (const float v) { return v < 0.5f ? 2.0f * v : 1.0f + (v - 0.5f) * 6.0f; }

    // Inverse of normToSpeed.
    inline float speedToNorm (const float speed) { return speed < 1.0f ? 0.5f * speed : 0.5f + (speed - 1.0f) / 6.0f; }

    // snap: 0 = off, 1 = firmware steps, 2 = whole semitones from -24 to +24 (knob centre = 0).
    inline float snapPitch (const float v, const int snap)
    {
        if (snap == 1)
            return kSpeedSteps[(int) std::min (6.0f, std::max (0.0f, std::round (v * 6.0f)))];
        if (snap == 2)
        {
            const float st = std::round ((v - 0.5f) * 48.0f);
            return speedToNorm (std::pow (2.0f, st / 12.0f));
        }
        return v;
    }
}

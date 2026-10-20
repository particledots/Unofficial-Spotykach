// Unofficial Spotykach - own code (independent re-implementation), GNU AGPLv3 (see LICENSE).
// Not an official Synthux Academy product; not affiliated with Synthux Academy.
//
// Replaces the firmware's src/core/synclock.{h,cpp}. That file is "derived from Maximum MIDI
// Programmer's ToolKit (c) 1993-1998 Paul Messick" and the upstream repository does not
// include a licence for that lineage, so it is deliberately NOT shipped (decision D6).
//
// This class offers the interface the Driver uses. It is an independent re-implementation written
// from that interface and plain tempo arithmetic; it does not copy synclock.cpp. (Its author had
// read synclock.cpp, so this is not a formal clean-room.)
//
// Milestone 2 supports the INTERNAL clock only: it generates `ppqn_out` ticks per quarter note
// at the set tempo, called once per update interval. External/host sync is Milestone 4; until
// then an "external" tick simply emits the equivalent number of ticks at once (not a
// phase-locked implementation) and the plug-in never selects an external source.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>

namespace bleeptools
{
    class SynClock
    {
    public:
        void init (const uint32_t updateIntervalMks, const uint32_t ppqnOut)
        {
            intervalSeconds = (double) updateIntervalMks * 1e-6;
            ppqnOutValue = (int32_t) ppqnOut;
            updateTicksPerCall();
        }

        uint32_t ppqn_in() const { return (uint32_t) ppqnInValue; }
        void set_ppqn_in (const uint32_t v) { if (v > 0) ppqnInValue = (int32_t) v; }

        void tick (const bool external)
        {
            if (externalClock)
            {
                if (external && running) emit ((int) std::max (1, ppqnOutValue / ppqnInValue), true);
                return;
            }
            if (! running) return;
            phase += ticksPerCall;
            const auto whole = (int) phase;
            phase -= (double) whole;
            if (whole > 0) emit (whole, false);
        }

        void set_on_tick (std::function<void (const bool)> f) { onTick = std::move (f); }

        float tempo() { return (float) bpm; }

        void set_tempo (const float newBpm)
        {
            if (std::fabs (newBpm - (float) bpm) < 0.01f) return;
            bpm = newBpm;
            updateTicksPerCall();
        }

        void run() { reset(); running = true; }
        void stop() { running = false; }
        void reset() { phase = 0.0; }
        bool is_running() const { return running; }

        void set_external_clock (const bool on) { externalClock = on; }
        bool external_clock() const { return externalClock; }

    private:
        void updateTicksPerCall()
        {
            // ticks/second = bpm / 60 * ppqn_out;  ticks per update = that * interval
            ticksPerCall = (double) bpm / 60.0 * (double) ppqnOutValue * intervalSeconds;
        }

        void emit (const int count, const bool fromExternal)
        {
            if (! onTick) return;
            for (int i = 0; i < count - 1; ++i) onTick (false);
            onTick (fromExternal);
        }

        std::function<void (const bool)> onTick;
        double bpm = 120.0;
        double intervalSeconds = 0.002;
        double ticksPerCall = 0.192;
        double phase = 0.0;
        int32_t ppqnOutValue = 48;
        int32_t ppqnInValue = 4;
        bool running = false;
        bool externalClock = false;
    };
}

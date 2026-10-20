// Unofficial Spotykach - own code, GNU AGPLv3 (see LICENSE).
// Not an official Synthux Academy product; not affiliated with Synthux Academy.
//
// Stand-ins for the handful of libDaisy symbols the Spotykach core (src/core) refers to.
// libDaisy itself (STM32 hardware abstraction) is NOT used by the plug-in.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdarg>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <functional>
#include "EngineTime.h"

#define DSY_SDRAM_BSS
#define DSY_CLAMP(in, mn, mx) ((in) < (mn) ? (mn) : ((in) > (mx) ? (mx) : (in)))
#define FLT_FMT(n) "%d.%0*d"
#define FLT_VAR(n, x) 0, n, 0
#define FLT_FMT3 "%d.%03d"
#define FLT_VAR3(x) 0, 0

namespace daisy
{
    enum LoggerDestination { LOGGER_NONE, LOGGER_INTERNAL, LOGGER_EXTERNAL, LOGGER_SEMIHOST };

    // Logging is discarded in the plug-in.
    template <LoggerDestination d>
    struct Logger
    {
        static void Print (const char*, ...) {}
        static void PrintLine (const char*, ...) {}
        static void PrintLineV (const char*, va_list) {}
        static void StartLog (bool) {}
    };

    struct System
    {
        // Milliseconds of audio processed by this engine (see EngineTime.h).
        static uint32_t GetNow() { return usp::gEngineMs; }
    };

    // Same behaviour as the libDaisy timer, but driven by the engine clock above.
    struct StopwatchTimer
    {
        void Init() { start = usp::gEngineMs; }
        void Restart() { start = usp::gEngineMs; }
        bool HasPassedMs (uint32_t ms) const { return (uint32_t) (usp::gEngineMs - start) >= ms; }
        uint32_t start = 0;
    };
}

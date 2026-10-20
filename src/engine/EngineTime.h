// Unofficial Spotykach - own code, GNU AGPLv3 (see LICENSE).
// Not an official Synthux Academy product; not affiliated with Synthux Academy.
//
// A millisecond clock that advances with the *audio* (one engine chunk = 2 ms), not with
// the wall clock. The Spotykach core uses time in a few places (clock timeouts, tap tempo);
// on the hardware that is the Daisy system timer. In the plug-in the engine sets this value
// at the start of every chunk, so timing follows the host's transport and offline bounces.
#pragma once
#include <cstdint>

namespace usp
{
    // thread_local: each audio thread that runs an engine sets it before touching the core.
    inline thread_local uint32_t gEngineMs = 0;
}

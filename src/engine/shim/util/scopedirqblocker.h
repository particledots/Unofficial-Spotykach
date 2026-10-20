// Unofficial Spotykach - own code, GNU AGPLv3 (see LICENSE).
// On the hardware this disables interrupts around a few instructions. In the plug-in every
// call into the core happens on the single audio thread, so there is nothing to block.
#pragma once
namespace daisy
{
    struct ScopedIrqBlocker {};
}

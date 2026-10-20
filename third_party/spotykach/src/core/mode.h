#pragma once

#include <stdint.h>

namespace spotykach {

enum class Mode: uint8_t {
    Reel = 0,
    Slice = 1, 
    Drift = 2,
    None = 0xff
};
};

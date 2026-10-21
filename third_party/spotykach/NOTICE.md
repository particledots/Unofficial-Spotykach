# Upstream: Spotykach firmware (Synthux Academy)

- Repository: https://github.com/Synthux-Academy/Spotykach
- Commit: 7da80618c7e051189c575e766ff40594e622e910
- Licence: MIT, Copyright (c) 2026 Synthux Academy (see `LICENSE` here). Further notices are kept
  in the headers of the files that carry them (Infrasonic Audio LLC 2024, bleeptools 2026).
- This is an UNOFFICIAL port. Synthux Academy has not endorsed or reviewed these changes.

## Copied
`src/core/**`, `src/common.h`, `src/expose.h`, `src/nocopy.h`, `LICENSE`, `CREDITS.md`.

## Not copied
`src/hw/**` and `src/ui/**` (Daisy hardware and panel code; replaced by `src/engine/`),
and `src/core/synclock.cpp/.h` (no licence text in the file; replaced by an independent
re-implementation of its interface in `src/engine/shim/synclock.h`).

## Modified files (each also carries a "MODIFIED for Unofficial Spotykach (2026)" comment)
- `src/core/panner.h`: `std::random_device` -> `std::minstd_rand`.
- `src/core/fx/fx.flux.cpp`: variable-length array replaced by a constexpr-sized array (invalid in Clang).
- `src/core/deck.cpp`: function-local static `Event` moved to namespace scope.
- `src/core/core.cpp`, `src/core/core.h`: `Core::init` takes caller-supplied buffers instead of the
  Daisy SDRAM singleton (adds `Core::Buffers`).
- `src/core/generator.cpp`: guarded a modulus by zero in `apply_dimensions()` (Slice mode on an empty deck). ARM tolerates it, x86 raises SIGFPE.
- `src/core/core.cpp`, `src/core/core.h` (second change): per-deck input and output trims (`set_trim_in`, `set_trim_out`,
  applied in `Core::process`). The hardware has analog trimmers here; the firmware has no equivalent.

# How it works

`tools/ardbuild.py` wraps the sketch the way the Arduino IDE does. It adds
`#include "Arduino.h"`, generates function prototypes, and keeps `#line`
directives so compile errors point at the `.ino`. The sketch is compiled in
one translation unit with `core/ard_core.cpp`, then linked with
`core/ard_mp.c`, the module entry point that calls CircuitPython. The build
uses CircuitPython's `py/dynruntime.mk` plus `core/cxx.mk`.

At import, `ard_mp.c` runs the sketch's global constructors and exposes
`setup()` and `loop()`. `core/arduino.py` calls them.

## Workarounds in the build

These are all in `core/cxx.mk` and `core/ard_mp.c`.

- `dynruntime.mk` has no C++ rules. `cxx.mk` adds them, with no exceptions,
  RTTI or thread-safe statics.
- `mpy_ld.py` silently drops `.init_array`, so global constructors never run.
  objcopy renames the translation unit's `_GLOBAL__sub_I_*` function to
  `ard_ctors` and makes it global, and `mpy_init` calls it. Renaming
  `.init_array` itself does not work, because its `R_ARM_TARGET1` relocation
  is not supported.
- `mpy_ld.py` rejects non-empty `.data` and static `.bss`. The sketch is built
  with `-fno-zero-initialized-in-bss`, and objcopy renames `.data*` sections to
  `.data.rel.ro.*`, which are kept. Native code is loaded into RAM, so the
  data stays writable.
- The RP2350 firmware is built with `-mfloat-abi=softfp`, but `dynruntime.mk`
  builds armv7emsp modules with `-mfloat-abi=hard`. A float passed through
  the native function table arrives as garbage, so the core never passes
  floats across that boundary. `delay()` lets CircuitPython do the division.
  Floats inside the sketch are fine.
- `millis()` and `micros()` count from when the sketch module loads, which is
  what an Arduino sketch sees after reset.

## Measured on the Metro RP2350

| Call | Result |
|---|---|
| `delay(100)` | 100.04 ms |
| `delay(1000)` | 1000.09 ms |
| `delayMicroseconds(500)` | 610 us |
| `millis()` | about 26 us per call |

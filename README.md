# arduino-on-circuitpython

Run simple Arduino sketches on CircuitPython. `ardbuild.py` compiles an
unmodified `.ino` into a native `.mpy` module, and a small fake Arduino core
maps `pinMode`, `digitalWrite`, `delay`, `millis`, `Serial` and friends onto
CircuitPython's `board`, `digitalio`, `analogio`, `pwmio` and `time`.

This is a proof of concept. Tested on an Adafruit Metro RP2350.

## Requirements

- CircuitPython firmware built with `CIRCUITPY_LOAD_NATIVE=1`, so it can load
  native `.mpy` modules. Stock builds have this off.
- A CircuitPython source tree, for the native module headers and linker.
- An ARM toolchain (`arm-none-eabi-gcc` and `g++`).
- Python packages: `pip install pyelftools ar`

## Blink walkthrough

Build the stock Arduino Blink example and install it on the board:

```sh
export MPY_DIR=~/circuitpython
python3 tools/ardbuild.py examples/Blink/Blink.ino --drive /Volumes/CIRCUITPY --code-py
```

```
built examples/Blink/blink.mpy (5955 bytes, armv7emsp)
installed lib/blink.mpy and lib/arduino.py
saved the old code.py as code.py.bak
wrote code.py
```

The chip architecture is read from the board's `boot_out.txt`. If yours does
not list it, pass `--arch armv7emsp` (or `armv6m`, `armv7m` and so on) instead.

`code.py` now has two lines:

```python
import arduino
arduino.run("blink")
```

The board reloads and the LED blinks once a second. `run()` calls `setup()`
once and then `loop()` forever. Ctrl-C stops it like any other program.

Leave off `--code-py` to keep your own `code.py`; the two lines are printed
for you instead.

## What works

Stock examples that ran on the Metro RP2350: Blink, BlinkWithoutDelay, Fade,
Button, AnalogReadSerial, ReadAnalogVoltage, DigitalReadSerial,
AnalogInOutSerial, Smoothing, switchCase, ForLoopIteration, Arrays and
Calibration.

- Pin `n` is `board.D<n>`, `A0` is `board.A0`, `LED_BUILTIN` is `board.LED`.
  Pin 13 falls back to `board.LED` if the board has no `D13`.
- `Serial.print` goes to the CircuitPython console.
- Global objects, classes, `new` and `delete` work.

## Limits

- No interrupts, `tone()` or `pulseIn()`. Using them is a compile error.
- No `Wire`, `SPI`, `String` or `Serial` input yet.
- A loop that never calls `delay()` and never returns, like
  `while (true) {}`, locks up USB until the board is reset.
- Each pin call goes through Python, so it is fine for blinking and buttons,
  not for bit-banging.

See [NOTES.md](NOTES.md) for how it works.

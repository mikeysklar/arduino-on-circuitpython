# SPDX-FileCopyrightText: 2026 Mikey Sklar for Adafruit Industries
# SPDX-License-Identifier: MIT
"""Run an Arduino sketch that ardbuild.py compiled into a native module.

    import arduino
    arduino.run("blink")
"""


def run(sketch):
    """Import the sketch module, call setup() once, then loop() forever."""
    if isinstance(sketch, str):
        sketch = __import__(sketch)
    sketch.setup()
    loop = sketch.loop
    while True:
        loop()

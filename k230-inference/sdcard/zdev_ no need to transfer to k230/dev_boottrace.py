# dev_boottrace.py -- localize the startup stall.
#
# Mirrors main.py's startup step-for-step, but:
#   * times every step (ticks_ms delta)
#   * lights the NeoPixel a DISTINCT color BEFORE each step, so if a step
#     hangs you can SEE which one from the LED -- no USB/print needed.
#
# Usage: rename to main.py (or run from the IDE) and power-cycle a few times.
# Note the color it freezes on, and/or read the [trace] timing lines.
#
# LED color  -> stage about to run
#   white    -> status_led up, about to Sensor()/reset()
#   red      -> sensor.reset()            <-- camera probe/retry (top suspect)
#   yellow   -> camera.apply_config()
#   cyan     -> Display.init(to_ide=...)
#   magenta  -> MediaManager.init()
#   blue     -> sensor.run() + settle
#   green    -> Detector() (kmodel load)
#   off then green blink x3 -> reached end OK

import gc
import sys
import time

for _p in ("/sdcard/app", "/sdcard/customLib"):
    if _p not in sys.path:
        sys.path.insert(0, _p)

from machine import Pin
import neopixel

from media.sensor import *
from media.display import *
from media.media import *

import config
import camera
import detector as detmod
import robot_io

# --- raw LED so we don't depend on status_led's named states ---
_np = neopixel.NeoPixel(Pin(config.NEOPIXEL_PIN), config.NEOPIXEL_PIXELS)
def led(r, g, b):
    for i in range(config.NEOPIXEL_PIXELS):
        _np[i] = (r, g, b)
    _np.write()

_t0 = time.ticks_ms()
def mark(color, name):
    """Light `color`, print the time since the PREVIOUS mark."""
    global _t0
    now = time.ticks_ms()
    print("[trace] {:>22} +{}ms".format("(prev done)", time.ticks_diff(now, _t0)))
    _t0 = now
    print("[trace] --> {}".format(name))
    led(*color)

# Toggle this to test the to_ide theory: set False and see if the stall goes away.
TO_IDE = True

def main():
    import nncase_runtime as nn

    mark((40, 40, 40), "Sensor() + sensor.reset()")
    sensor = Sensor()
    led(60, 0, 0)                      # red: reset() is the risky one
    sensor.reset()

    mark((60, 60, 0), "camera.apply_config()")
    camera.apply_config(sensor)
    camera.setup_button()

    mark((0, 60, 60), "Display.init(to_ide={})".format(TO_IDE))
    if config.SHOW_DISPLAY:
        Display.init(Display.VIRT, sensor.width(), sensor.height(), to_ide=TO_IDE)

    mark((60, 0, 60), "MediaManager.init()")
    MediaManager.init()

    mark((0, 0, 60), "sensor.run() + settle")
    sensor.run()
    time.sleep_ms(config.SETTLE_MS)

    mark((0, 60, 0), "Detector() kmodel load")
    det = detmod.Detector(sensor)

    mark((0, 30, 0), "open_link() + DONE")
    u = robot_io.open_link()

    # success: blink green 3x
    for _ in range(3):
        led(0, 80, 0); time.sleep_ms(150)
        led(0, 0, 0);  time.sleep_ms(150)
    print("[trace] startup complete, all stages passed")

    # idle so you can confirm we got here
    while True:
        led(0, 40, 0); time.sleep_ms(500)
        led(0, 0, 0);  time.sleep_ms(500)


if __name__ == "__main__":
    main()

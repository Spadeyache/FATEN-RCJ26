# main_ledonly.py -- minimal boot test.
#
# Does NOTHING except set the NeoPixel to white, then hold it.
# No camera, no media, no model, no UART, no config/app imports.
#
# If this lights white  -> MicroPython + main.py DO run; the stall is in the
#                          heavy startup (sensor/display/media/model).
# If it stays dark      -> the problem is below Python (firmware/USB/boot).
#
# Usage: copy to the device as /sdcard/main.py and power-cycle.

import time
from machine import Pin
import neopixel

NEOPIXEL_PIN    = 35   # matches config.NEOPIXEL_PIN
NEOPIXEL_PIXELS = 1

np = neopixel.NeoPixel(Pin(NEOPIXEL_PIN), NEOPIXEL_PIXELS)

for i in range(NEOPIXEL_PIXELS):
    np[i] = (255, 255, 255)   # white
np.write()

print("led-only: NeoPixel set white")

# Hold so you can confirm it stays on.
while True:
    time.sleep_ms(1000)

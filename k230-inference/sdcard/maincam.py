# K230D Zero / CanMV v1.5-legacy
#
# maincam.py -- dataset capture tool.
#
#   Same camera config as main.py (exposure, gain, AWB, orientation).
#   No inference, no UART. Live preview with BOOT-button capture.
#
#   Press BOOT -> saves /data/captures/cap_NNNN.jpg
#   Ctrl+C     -> stop
#
# Tunables: sdcard/app/config.py (exposure, gain, capture dir, etc.)

import gc
import sys
import time

for _p in ("/sdcard/app", "/sdcard/customLib"):
    if _p not in sys.path:
        sys.path.insert(0, _p)

from media.sensor import *
from media.display import *
from media.media import *

import config
import camera
import status_led


def main():
    print("=== maincam.py === capture mode")
    print("  BOOT button -> save frame")
    print("  Ctrl+C      -> stop")
    print("  Save dir    :", config.CAPTURE_DIR)
    print("  Exposure    :", config.EXPOSURE_US, "us")
    print("  Gain        :", config.GAIN_DB, "dB")

    status_led.init()
    status_led.set_status("evac", force=True)

    sensor = Sensor()
    sensor.reset()
    camera.apply_config(sensor)
    camera.setup_button()

    if config.SHOW_DISPLAY:
        Display.init(Display.VIRT, sensor.width(), sensor.height(), to_ide=True)
    MediaManager.init()
    sensor.run()
    time.sleep_ms(config.SETTLE_MS)

    frame    = 0
    captured = 0
    t_window = time.ticks_ms()

    try:
        while True:
            img = camera.get_image(sensor)

            saved = camera.maybe_save(img)
            if saved:
                captured += 1
                print("saved [{:4d}] -> {}".format(captured, saved))
                status_led.set_status("found", force=True)
                time.sleep_ms(300)
                status_led.set_status("evac", force=True)

            if config.SHOW_DISPLAY:
                Display.show_image(img)

            frame += 1
            if (frame & 0x1F) == 0:
                gc.collect()

            if frame % config.DEBUG_EVERY == 0:
                now = time.ticks_ms()
                fps = config.DEBUG_EVERY * 1000 / max(
                    1, time.ticks_diff(now, t_window))
                print("f={:5d}  fps={:5.2f}  total_saved={}".format(
                    frame, fps, captured))
                t_window = now

    except KeyboardInterrupt:
        print("\nstopping...")
    finally:
        sensor.stop()
        if config.SHOW_DISPLAY:
            Display.deinit()
        MediaManager.deinit()
        print("done. {} frames captured.".format(captured))


if __name__ == "__main__":
    main()

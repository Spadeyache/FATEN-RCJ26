# K230D Zero / CanMV v1.5-legacy
#
# maincam.py -- image capture tool for dataset collection.
#
#   Same camera config as main.py (exposure, gain, AWB, orientation).
#   No inference, no UART.
#   Press the BOOT button to save a frame to CAPTURE_DIR.
#
# Saved files: /data/k230-train/captures/cap_NNNN.jpg
# Edit camera_vision.py to change exposure / gain / orientation / save dir.

import gc
import sys
import time

if "/data/src" not in sys.path:
    sys.path.insert(0, "/data/src")

from media.sensor import *
from media.display import *
from media.media import *

import camera_vision

SETTLE_MS   = 200
DEBUG_EVERY = 60   # print fps every N frames


def main():
    print("=== maincam.py === capture mode")
    print("  BOOT button -> save frame")
    print("  Ctrl+C      -> stop")
    print("  Save dir    :", camera_vision.CAPTURE_DIR)
    print("  Exposure    :", camera_vision.EXPOSURE_US, "us")
    print("  Gain        :", camera_vision.GAIN_DB, "dB")

    sensor = Sensor()
    sensor.reset()
    camera_vision.apply_config(sensor)
    camera_vision.setup_button()
    Display.init(Display.VIRT, sensor.width(), sensor.height(), to_ide=True)
    MediaManager.init()
    sensor.run()
    time.sleep_ms(SETTLE_MS)

    frame    = 0
    captured = 0
    t_window = time.ticks_ms()

    try:
        while True:
            img = camera_vision.get_image(sensor)

            saved = camera_vision.maybe_save(img)
            if saved:
                captured += 1
                print("saved [{:4d}] -> {}".format(captured, saved))

            Display.show_image(img)
            frame += 1

            if (frame & 0x1F) == 0:
                gc.collect()

            if frame % DEBUG_EVERY == 0:
                now = time.ticks_ms()
                fps = DEBUG_EVERY * 1000 / max(
                    1, time.ticks_diff(now, t_window))
                print("f={:5d}  fps={:5.2f}  total_saved={}".format(
                    frame, fps, captured))
                t_window = now

    except KeyboardInterrupt:
        print("\nstopping...")
    finally:
        sensor.stop()
        Display.deinit()
        MediaManager.deinit()
        print("done. {} frames captured.".format(captured))


if __name__ == "__main__":
    main()

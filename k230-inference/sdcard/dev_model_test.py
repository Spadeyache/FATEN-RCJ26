# dev_model_test.py -- load victim.kmodel and run preview inference continuously.

import gc
import sys
import time

for _p in ("/sdcard/app", "/sdcard/config", "/sdcard/customLib"):
    if _p not in sys.path:
        sys.path.insert(0, _p)

from media.sensor import *
from media.display import *
from media.media import *

import camera_profile as profile
import camera_setup
import victim_detector


def main():
    import nncase_runtime as nn

    print("=== dev_model_test.py ===")
    sensor = Sensor()
    sensor.reset()
    camera_setup.apply_config(sensor)

    if profile.SHOW_DISPLAY:
        Display.init(Display.VIRT, sensor.width(), sensor.height(), to_ide=True)
    MediaManager.init()
    sensor.run()
    time.sleep_ms(profile.SETTLE_MS)

    det = victim_detector.VictimDetector(sensor)
    frame = 0
    t_window = time.ticks_ms()

    try:
        while True:
            img = camera_setup.get_image(sensor)
            boxes = det.infer(img)
            if profile.SHOW_DISPLAY:
                for d in boxes:
                    cls_id = int(d[0])
                    color = profile.COLOR_PALETTE[cls_id % len(profile.COLOR_PALETTE)]
                    x1, y1, x2, y2 = d[2], d[3], d[4], d[5]
                    img.draw_rectangle(int(x1), int(y1),
                                       int(x2 - x1), int(y2 - y1),
                                       color=color, thickness=2)
                    name = det.labels[cls_id] if cls_id < len(det.labels) else "c{}".format(cls_id)
                    img.draw_string_advanced(int(x1), max(0, int(y1) - 20), 16,
                                             "{} {:.2f}".format(name, float(d[1])),
                                             color=color)
                Display.show_image(img)

            frame += 1
            if (frame & 0x1F) == 0:
                gc.collect()
            if frame % profile.DEBUG_EVERY == 0:
                now = time.ticks_ms()
                fps = profile.DEBUG_EVERY * 1000 / max(1, time.ticks_diff(now, t_window))
                print("f={:5d} fps={:5.2f} dets={}".format(frame, fps, len(boxes)))
                t_window = now
    except KeyboardInterrupt:
        print("\nstopping...")
    finally:
        det.release()
        sensor.stop()
        if profile.SHOW_DISPLAY:
            Display.deinit()
        MediaManager.deinit()
        try:
            nn.shrink_memory_pool()
        except Exception:
            pass
        print("done.")


if __name__ == "__main__":
    main()

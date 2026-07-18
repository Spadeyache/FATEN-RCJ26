# K230D Zero / CanMV v1.5-legacy
#
# dev_points.py -- standalone test for the POINTS model only.
#
#   Loads /data/models/point_deploy_config.json (Green/Red), runs inference on
#   the live camera and draws boxes on the preview. No UART, no model swap.
#
#   Ctrl+C -> stop
#
# Run manually from the IDE/REPL; main.py does not import this.

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
import detector as detmod
import status_led


def main():
    import nncase_runtime as nn

    print("=== dev_points.py === points model only")
    status_led.init()
    status_led.set_status("boot", force=True)

    sensor = Sensor()
    sensor.reset()
    camera.apply_config(sensor)
    camera.setup_button()
    if config.SHOW_DISPLAY:
        Display.init(Display.VIRT, sensor.width(), sensor.height(), to_ide=True)
    MediaManager.init()
    sensor.run()
    time.sleep_ms(config.SETTLE_MS)

    # Points model only (Green/Red). Still white/boot here -- this is the load.
    det = detmod.Detector(sensor, config.POINTS_DEPLOY_CONFIG)

    frame    = 0
    t_window = time.ticks_ms()

    try:
        while True:
            img = camera.get_image(sensor)

            boxes = det.infer(img)
            status_led.set_status("found" if len(boxes) > 0 else "run")

            if config.SHOW_DISPLAY:
                for d in boxes:
                    cls_id = int(d[0])
                    color = config.COLOR_PALETTE[cls_id % len(config.COLOR_PALETTE)]
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
            if frame % config.DEBUG_EVERY == 0:
                now = time.ticks_ms()
                fps = config.DEBUG_EVERY * 1000 / max(
                    1, time.ticks_diff(now, t_window))
                print("f={:5d} fps={:5.2f} dets={}".format(frame, fps, len(boxes)))
                for d in boxes:
                    cls_id = int(d[0])
                    name = det.labels[cls_id] if cls_id < len(det.labels) else "c{}".format(cls_id)
                    print("   {} {:.2f}".format(name, float(d[1])))
                t_window = now

    except KeyboardInterrupt:
        print("\nstopping...")
    finally:
        status_led.set_status("rest", force=True)
        det.release()
        sensor.stop()
        if config.SHOW_DISPLAY:
            Display.deinit()
        MediaManager.deinit()
        try: nn.shrink_memory_pool()
        except Exception: pass
        print("done.")


if __name__ == "__main__":
    main()

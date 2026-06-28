# K230D Zero / CanMV v1.5-legacy
#
# main.py -- Teensy-controlled YOLOv8 detection loop.
#
#   IDLE  (run_state=False)  -> no inference, no UART traffic, live preview only
#   RUN   (run_state=True)   -> camera.apply_config re-applied + full inference,
#                               ALL surviving boxes streamed to Teensy
#
# Transitions are driven by single-byte commands from the Teensy via
# robot_io.read_command (0x00 idle, 0x01 run). Camera config is re-applied
# on every IDLE -> RUN edge so a calibration change is picked up next run.

import gc
import sys
import time

# boot.py already wired /sdcard/app + /sdcard/customLib into sys.path. Repeat
# it here as a guard in case main.py is invoked manually before boot ran.
for _p in ("/sdcard/app", "/sdcard/customLib"):
    if _p not in sys.path:
        sys.path.insert(0, _p)

from media.sensor import *
from media.display import *
from media.media import *

import config
import camera
import detector as detmod
import robot_io
import status_led


def main():
    import nncase_runtime as nn

    print("=== main.py === Teensy-controlled YOLO")
    status_led.init()
    status_led.set_status("rest", force=True)

    # Camera + display + media up. Config is re-applied on every RUN edge.
    sensor = Sensor()
    sensor.reset()
    camera.apply_config(sensor)
    camera.setup_button()
    if config.SHOW_DISPLAY:
        Display.init(Display.VIRT, sensor.width(), sensor.height(), to_ide=True)
    MediaManager.init()
    sensor.run()
    time.sleep_ms(config.SETTLE_MS)
    status_led.set_status("rest", force=True)

    # Model + ai2d. Starts on the victims model; Teensy can swap to points.
    det = detmod.Detector(sensor)
    current_model = "victims"

    # UART link.
    u = robot_io.open_link()
    print("Boot state is RUN. Send 0x00 from Teensy to idle/rest.")

    run_state  = True
    prev_state = True
    frame      = 0
    t_window   = time.ticks_ms()

    try:
        while True:
            run_state = robot_io.read_command(u, run_state)

            # Model swap request from Teensy (victims <-> points). Releasing the
            # current KPU before loading the new one keeps only one model in RAM.
            model_req = robot_io.take_model_request()
            if model_req is not None and model_req != current_model:
                cfg_path = (config.POINTS_DEPLOY_CONFIG if model_req == "points"
                            else config.DEPLOY_CONFIG_PATH)
                print("-> swap model:", current_model, "->", model_req)
                status_led.set_status("rest", force=True)
                try:
                    det.release()
                    det = detmod.Detector(sensor, cfg_path)
                    current_model = model_req
                except Exception as e:
                    print("model swap FAILED:", e)
                status_led.set_status("evac", force=True)

            # IDLE -> RUN edge: re-apply camera config.
            if run_state and not prev_state:
                print("-> RUN  (resetting camera + re-applying camera config)")
                try:
                    sensor.stop()
                except Exception:
                    pass
                sensor.reset()
                camera.apply_config(sensor)
                sensor.run()
                time.sleep_ms(config.SETTLE_MS)
                status_led.set_status("evac", force=True)
            elif not run_state and prev_state:
                print("-> IDLE")
                status_led.set_status("rest", force=True)
            prev_state = run_state

            if not run_state:
                # IDLE: preview only, no inference, no UART.
                img = camera.get_image(sensor)
                if config.SHOW_DISPLAY:
                    Display.show_image(img)
                time.sleep_ms(20)
                continue

            # RUN: full pipeline.
            img = camera.get_image(sensor)
            saved = camera.maybe_save(img)
            if saved:
                print("saved ->", saved)

            boxes = det.infer(img)               # list of [cls, score, x1,y1,x2,y2]
            status_led.set_status("found" if len(boxes) > 0 else "evac")
            robot_io.send_boxes(u, boxes)

            # Draw same boxes on preview.
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
                print("f={:5d} fps={:5.2f} run={} dets={}".format(
                    frame, fps, run_state, len(boxes)))
                t_window = now

    except KeyboardInterrupt:
        print("\nstopping...")
    finally:
        status_led.set_status("rest", force=True)
        try: u.deinit()
        except Exception: pass
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

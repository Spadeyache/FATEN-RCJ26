# K230D Zero / CanMV v1.5-legacy
#
# main.py -- Teensy-controlled YOLOv8 detection loop.
#
# Boots into RUN, runs inference and streams boxes to the Teensy. The Teensy
# drives idle/run state and can request a model swap. Status LED shows the
# current phase (boot / running / detection).

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


# draw_string_advanced() needs a FreeType TTF font, which this legacy K230D
# firmware doesn't ship ("FreeType init failed, font (null)"). Prefer the
# built-in bitmap font (draw_string, no FreeType); fall back to the advanced
# call only if draw_string is missing, and if BOTH fail just disable text so a
# font error can never take down the detection/UART loop -- boxes still draw.
_text_ok = True


def _clip_int(v, lo, hi):
    try:
        v = int(v)
    except Exception:
        return lo
    if v < lo:
        return lo
    if v > hi:
        return hi
    return v


def _sanitize_boxes(boxes, width, height, num_classes):
    """Return display/UART-safe boxes, dropping malformed detections."""
    clean = []
    for b in boxes:
        try:
            cls_id = int(b[0])
            score = float(b[1])
            x1 = _clip_int(b[2], 0, width - 1)
            y1 = _clip_int(b[3], 0, height - 1)
            x2 = _clip_int(b[4], 0, width - 1)
            y2 = _clip_int(b[5], 0, height - 1)
        except Exception as e:
            print("box dropped (parse failed):", e)
            continue
        if cls_id < 0 or cls_id >= num_classes:
            print("box dropped (bad class):", cls_id)
            continue
        if score < 0.0:
            score = 0.0
        if score > 1.0:
            score = 1.0
        if x2 < x1:
            x1, x2 = x2, x1
        if y2 < y1:
            y1, y2 = y2, y1
        if x2 <= x1 or y2 <= y1:
            continue
        clean.append([cls_id, score, x1, y1, x2, y2])
    clean.sort(key=lambda d: -float(d[1]))
    return clean


def _draw_label(img, x, y, text, color):
    global _text_ok
    if not (_text_ok and getattr(config, "DRAW_LABELS", True)):
        return
    try:
        img.draw_string(int(x), int(y), text, color=color, scale=2)
        return
    except Exception:
        pass
    try:
        img.draw_string_advanced(int(x), int(y), 16, text, color=color)
        return
    except Exception as e:
        print("label text disabled (draw failed):", e)
        _text_ok = False


def main():
    # LED alive immediately (cyan) so the slow nncase import + 3.5MB kmodel
    # load doesn't look like a hung board. Stays cyan until the loop runs.
    print("=== main.py === Teensy-controlled YOLO")
    status_led.init()
    status_led.set_status("boot", force=True)

    import nncase_runtime as nn

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

    # Model + ai2d. Starts on the colordet model; Teensy can swap to points.
    # Still cyan here -- this is the slow part. The loop switches to white/green.
    det = detmod.Detector(sensor)
    current_model = "colordet"

    # UART link.
    u = robot_io.open_link()
    print("Boot state is RUN. Send 0x00 from Teensy to idle/rest.")

    run_state  = True
    prev_state = True
    frame      = 0
    t_window   = time.ticks_ms()

    try:
        while True:
            status_led.refresh()
            run_state = robot_io.read_command(u, run_state)

            # Model swap request from Teensy (victims <-> points). Releasing the
            # current KPU before loading the new one keeps only one model in RAM.
            model_req = robot_io.take_model_request()
            if model_req is not None and model_req != current_model:
                cfg_path = (config.POINTS_DEPLOY_CONFIG if model_req == "points"
                            else config.DEPLOY_CONFIG_PATH)
                print("-> swap model:", current_model, "->", model_req)
                status_led.set_status("boot", force=True)   # cyan: loading kmodel
                try:
                    det.release()
                    det = detmod.Detector(sensor, cfg_path)
                    current_model = model_req
                except Exception as e:
                    print("model swap FAILED:", e)
                status_led.set_status("run", force=True)

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
                status_led.set_status("run", force=True)
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
            t_a = time.ticks_ms()
            img = camera.get_image(sensor)
            saved = camera.maybe_save(img)
            if saved:
                print("saved ->", saved)
            t_b = time.ticks_ms()

            try:
                boxes = det.infer(img)           # ALL boxes >= CONF_THRESHOLD,
                                                 # [cls, score, x1,y1,x2,y2],
                                                 # sorted high->low confidence
            except MemoryError as e:
                print("infer skipped (ndarray malloc fail):", e)
                try:
                    gc.collect()
                except Exception:
                    pass
                boxes = []
            boxes = _sanitize_boxes(boxes, sensor.width(), sensor.height(),
                                    len(det.labels))
            status_led.set_status("found" if len(boxes) > 0 else "run")

            # Teensy only wants the strongest few targets: send the top-N by
            # confidence. The K230 preview below still draws every box above
            # threshold.
            t_c = time.ticks_ms()
            tx_boxes = boxes
            if len(tx_boxes) > config.TX_TOP_N:
                tx_boxes = sorted(boxes, key=lambda d: -float(d[1]))[:config.TX_TOP_N]
            try:
                robot_io.send_boxes(u, tx_boxes)
            except Exception as e:
                print("uart send skipped (box error):", e)

            # Draw ALL detected boxes on preview (not just the ones sent).
            show_this_frame = (config.SHOW_DISPLAY and
                               (frame % getattr(config, "DISPLAY_EVERY_N", 1) == 0))
            if show_this_frame and getattr(config, "DRAW_BOXES", True):
                draw_boxes = boxes
                max_draw = getattr(config, "MAX_BOXES_DRAW", 12)
                if len(draw_boxes) > max_draw:
                    draw_boxes = draw_boxes[:max_draw]
                for d in draw_boxes:
                    try:
                        cls_id = int(d[0])
                        color = config.COLOR_PALETTE[cls_id % len(config.COLOR_PALETTE)]
                        x1, y1, x2, y2 = d[2], d[3], d[4], d[5]
                        img.draw_rectangle(int(x1), int(y1),
                                           int(x2 - x1), int(y2 - y1),
                                           color=color, thickness=2)
                        name = det.labels[cls_id] if cls_id < len(det.labels) else "c{}".format(cls_id)
                        _draw_label(img, int(x1), max(0, int(y1) - 16),
                                    "{} {:.2f}".format(name, float(d[1])), color)
                    except Exception as e:
                        print("draw box skipped:", e)
            if show_this_frame:
                try:
                    Display.show_image(img)
                except Exception as e:
                    print("display show skipped:", e)

            t_d = time.ticks_ms()

            frame += 1
            if (frame & 0x1F) == 0:
                gc.collect()
            if frame % config.DEBUG_EVERY == 0:
                now = time.ticks_ms()
                fps = config.DEBUG_EVERY * 1000 / max(
                    1, time.ticks_diff(now, t_window))
                print("f={:5d} fps={:5.2f} run={} dets={}".format(
                    frame, fps, run_state, len(boxes)))
                if config.PROFILE_TIMING:
                    # Whole-loop split: cap(snapshot) / inf(prep+kpu+decode) /
                    # disp(draw+show_image). The `timing:` line only covers inf.
                    print("loop: cap={}ms inf={}ms disp={}ms".format(
                        time.ticks_diff(t_b, t_a),
                        time.ticks_diff(t_c, t_b),
                        time.ticks_diff(t_d, t_c)))
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

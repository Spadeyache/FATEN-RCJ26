# K230D Zero / CanMV v1.5-legacy
#
# main2.py -- Teensy-controlled YOLO loop.
#
#   IDLE  (run_state=False)  -> no inference, no UART traffic
#   RUN   (run_state=True)   -> camera_vision.apply_config() re-applied,
#                                full YOLOv8 anchor-free inference,
#                                ALL surviving boxes -> Teensy
#
# Transitions are driven by single-byte commands from the Teensy via
# TeensyComm.read_command (0x00 idle, 0x01 run). Camera config is
# re-applied on every IDLE -> RUN edge (per user request) so a tuning
# change in camera_vision.py is picked up the next time the Teensy
# asks for a run.

import gc
import sys
import time

# Modules sit at /data/scr on the SD card.
if "/data/scr" not in sys.path:
    sys.path.insert(0, "/data/scr")

from media.sensor import *
from media.display import *
from media.media import *

import camera_vision
import TeensyComm


# ============================================================================
# Tunables
# ============================================================================

ROOT_PATH           = "/data/k230-train"
DEPLOY_CONFIG_PATH  = ROOT_PATH + "/deploy_config.json"
SETTLE_MS           = 200

NUM_ANCHORS_640x480 = 6300                       # 80*60 + 40*30 + 20*15
CONF_THRESHOLD      = 0.30
NMS_THRESHOLD       = 0.50

COLOR_PALETTE       = [(220, 20, 60), (119, 11, 32),
                       (0,   0, 142), (0,   0, 230)]
DEBUG_EVERY         = 30


# ============================================================================
# YOLOv8 anchor-free decoder
# ============================================================================

def _iou(a, b):
    ix1 = a[0] if a[0] > b[0] else b[0]
    iy1 = a[1] if a[1] > b[1] else b[1]
    ix2 = a[2] if a[2] < b[2] else b[2]
    iy2 = a[3] if a[3] < b[3] else b[3]
    iw = ix2 - ix1
    ih = iy2 - iy1
    if iw <= 0 or ih <= 0:
        return 0.0
    inter = iw * ih
    aa = (a[2] - a[0]) * (a[3] - a[1])
    bb = (b[2] - b[0]) * (b[3] - b[1])
    union = aa + bb - inter
    if union <= 0:
        return 0.0
    return inter / union


def _decode_yolov8_anchorfree(flat_out, num_classes, conf_thr,
                               num_anchors=NUM_ANCHORS_640x480):
    N = num_anchors
    off_cx, off_cy, off_w, off_h, off_c = 0, N, 2 * N, 3 * N, 4 * N
    boxes = []
    for n in range(N):
        best_c = 0
        best_s = flat_out[off_c + n]
        for c in range(1, num_classes):
            s = flat_out[off_c + c * N + n]
            if s > best_s:
                best_s = s
                best_c = c
        if best_s < conf_thr:
            continue
        cx = flat_out[off_cx + n]
        cy = flat_out[off_cy + n]
        bw = flat_out[off_w  + n]
        bh = flat_out[off_h  + n]
        hw = bw / 2
        hh = bh / 2
        boxes.append([best_c, float(best_s),
                      float(cx - hw), float(cy - hh),
                      float(cx + hw), float(cy + hh)])
    return boxes


def _nms_class_wise(boxes, iou_thr):
    kept = []
    classes = set()
    for b in boxes:
        classes.add(b[0])
    for c in classes:
        cb = [b for b in boxes if b[0] == c]
        cb.sort(key=lambda b: -b[1])
        while cb:
            head = cb.pop(0)
            kept.append(head)
            cb = [b for b in cb if _iou(head[2:6], b[2:6]) < iou_thr]
    kept.sort(key=lambda b: -b[1])
    return kept


def _unletterbox_box(box_xyxy, ratio, left_pad, top_pad, ori_w, ori_h):
    x1 = (box_xyxy[0] - left_pad) / ratio
    y1 = (box_xyxy[1] - top_pad)  / ratio
    x2 = (box_xyxy[2] - left_pad) / ratio
    y2 = (box_xyxy[3] - top_pad)  / ratio
    if x1 < 0: x1 = 0.0
    if y1 < 0: y1 = 0.0
    if x2 > ori_w: x2 = float(ori_w)
    if y2 > ori_h: y2 = float(ori_h)
    return [x1, y1, x2, y2]


# ============================================================================
# Helpers
# ============================================================================

def _load_deploy_config():
    import ujson
    with open(DEPLOY_CONFIG_PATH, "r") as f:
        cfg = ujson.load(f)
    if not cfg["kmodel_path"].startswith("/"):
        cfg["kmodel_path"] = (
            DEPLOY_CONFIG_PATH.rsplit("/", 1)[0] + "/" + cfg["kmodel_path"]
        )
    print("Loaded deploy_config:", DEPLOY_CONFIG_PATH)
    print("  kmodel    :", cfg["kmodel_path"])
    print("  img_size  :", cfg["img_size"])
    print("  categories:", cfg["categories"])
    print("  model_type:", cfg["model_type"])
    if cfg.get("model_type") != "AnchorFreeDet":
        raise ValueError("main2.py requires model_type=AnchorFreeDet")
    return cfg


def _chw_from_grayscale(img):
    import ulab.numpy as np
    hwc = img.to_numpy_ref()
    shape = hwc.shape
    if len(shape) == 2:
        H, W = shape
        plane = hwc
    elif len(shape) == 3 and shape[2] == 1:
        H, W, _ = shape
        plane = hwc[:, :, 0]
    else:
        H, W, C = shape
        return hwc.reshape((H * W, C)).transpose().copy().reshape((C, H, W))
    chw = np.zeros((3, H, W), dtype=np.uint8)
    chw[0] = plane
    chw[1] = plane
    chw[2] = plane
    return chw


# ============================================================================
# Main
# ============================================================================

def main():
    import nncase_runtime as nn
    import ulab.numpy as np

    print("=== main2.py === Teensy-controlled YOLO")

    # Camera up (config will be re-applied on every idle->run edge below).
    sensor = Sensor()
    sensor.reset()
    camera_vision.apply_config(sensor)
    camera_vision.setup_button()
    Display.init(Display.VIRT, sensor.width(), sensor.height(), to_ide=True)
    MediaManager.init()
    sensor.run()
    time.sleep_ms(SETTLE_MS)

    # Model + ai2d.
    cfg = _load_deploy_config()
    kmodel_path = cfg["kmodel_path"]
    labels      = cfg["categories"]
    img_size    = cfg["img_size"]
    num_classes = cfg["num_classes"]
    sensor_w    = sensor.width()
    sensor_h    = sensor.height()
    model_w     = img_size[0]
    model_h     = img_size[1]

    kpu = nn.kpu()
    kpu.load_kmodel(kmodel_path)

    ai2d = nn.ai2d()
    ai2d.set_dtype(nn.ai2d_format.NCHW_FMT, nn.ai2d_format.NCHW_FMT,
                   np.uint8, np.uint8)
    ratio = min(model_w / sensor_w, model_h / sensor_h)
    new_w = int(ratio * sensor_w)
    new_h = int(ratio * sensor_h)
    dw = (model_w - new_w) / 2
    dh = (model_h - new_h) / 2
    top    = int(round(dh - 0.1))
    bottom = int(round(dh + 0.1))
    left   = int(round(dw - 0.1))
    right  = int(round(dw + 0.1))
    ai2d.set_pad_param(True, [0, 0, 0, 0, top, bottom, left, right], 0,
                       [114, 114, 114])
    ai2d.set_resize_param(True, nn.interp_method.tf_bilinear,
                          nn.interp_mode.half_pixel)
    ai2d_builder = ai2d.build([1, 3, sensor_h, sensor_w],
                              [1, 3, model_h, model_w])
    ai2d_out = nn.from_numpy(np.ones((1, 3, model_h, model_w), dtype=np.uint8))

    # UART link.
    u = TeensyComm.open_link()
    print("Wait state. Send 0x01 from Teensy to start detection.")

    run_state = False
    prev_state = False
    frame = 0
    t_window = time.ticks_ms()
    try:
        while True:
            run_state = TeensyComm.read_command(u, run_state)

            # IDLE -> RUN edge: re-apply camera config so any tuning change
            # in camera_vision.py is picked up on the new run.
            if run_state and not prev_state:
                print("-> RUN  (re-applying camera config)")
                camera_vision.apply_config(sensor)
            elif not run_state and prev_state:
                print("-> IDLE")
            prev_state = run_state

            if not run_state:
                # IDLE: show preview, no inference, no UART traffic.
                img = camera_vision.get_image(sensor)
                Display.show_image(img)
                time.sleep_ms(20)
                continue

            # RUN: full pipeline.
            img = camera_vision.get_image(sensor)
            saved = camera_vision.maybe_save(img)
            if saved:
                print("saved ->", saved)

            chw = _chw_from_grayscale(img)
            ai2d_input_tensor = nn.from_numpy(chw)
            ai2d_builder.run(ai2d_input_tensor, ai2d_out)
            del ai2d_input_tensor

            kpu.set_input_tensor(0, ai2d_out)
            kpu.run()

            results = []
            for i in range(kpu.outputs_size()):
                d = kpu.get_output_tensor(i)
                arr = d.to_numpy()
                total = 1
                for s in arr.shape:
                    total *= s
                results.append(arr.reshape((total,)))
                del d

            det = []
            try:
                if results:
                    letter = _decode_yolov8_anchorfree(
                        results[0], num_classes, CONF_THRESHOLD,
                        num_anchors=NUM_ANCHORS_640x480)
                    letter = _nms_class_wise(letter, NMS_THRESHOLD)
                    for b in letter:
                        xyxy = _unletterbox_box(b[2:6], ratio, left, top,
                                                 sensor_w, sensor_h)
                        det.append([b[0], b[1],
                                    xyxy[0], xyxy[1], xyxy[2], xyxy[3]])
            except Exception as e:
                if frame < 3:
                    print("postprocess error:", e)

            # Send every NMS-surviving box to the Teensy.
            # (det is exactly what gets drawn on the display below, so the
            #  HTML viewer's job is to reproduce the drawn picture from
            #  Teensy-side reception.)
            TeensyComm.send_boxes(u, det)

            # Draw the same set on the live preview.
            for d in det:
                cls_id = int(d[0])
                color = COLOR_PALETTE[cls_id % len(COLOR_PALETTE)]
                x1, y1, x2, y2 = d[2], d[3], d[4], d[5]
                img.draw_rectangle(int(x1), int(y1),
                                   int(x2 - x1), int(y2 - y1),
                                   color=color, thickness=2)
                name = labels[cls_id] if cls_id < len(labels) else "c{}".format(cls_id)
                img.draw_string_advanced(int(x1), max(0, int(y1) - 20), 16,
                                          "{} {:.2f}".format(name, float(d[1])),
                                          color=color)

            Display.show_image(img)
            frame += 1
            if (frame & 0x1F) == 0:
                gc.collect()
            if frame % DEBUG_EVERY == 0:
                now = time.ticks_ms()
                fps = DEBUG_EVERY * 1000 / max(
                    1, time.ticks_diff(now, t_window))
                print("f={:5d} fps={:5.2f} run={} dets={}".format(
                    frame, fps, run_state, len(det)))
                t_window = now

    except KeyboardInterrupt:
        print("\nstopping...")
    finally:
        try: u.deinit()
        except Exception: pass
        if hasattr(kpu, "deinit"):
            kpu.deinit()
        sensor.stop()
        Display.deinit()
        MediaManager.deinit()
        try: nn.shrink_memory_pool()
        except Exception: pass
        print("done.")


if __name__ == "__main__":
    main()

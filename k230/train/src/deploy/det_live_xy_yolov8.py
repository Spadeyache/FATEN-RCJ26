# K230D Zero / CanMV v1.5-legacy / YOLOv8 anchor-free
#
# Live grayscale + histeq inference using a YOLOv8 .kmodel. Sends the
# single highest-confidence detection's center over UART to the Teensy.
#
# Preprocessing MUST match calibration. The calibration JPGs in
# data/calibration_74/ are grayscale-histeq stored as 3-channel JPG (B=G=R
# at every pixel; checked with cv2.split). So the model was trained and
# calibrated on histeq'd-grayscale images replicated to 3 channels. The
# on-device pipeline reproduces that:
#   sensor = Sensor.GRAYSCALE -> img.histeq() -> replicate single plane
#   to 3 channels -> ai2d letterbox114 -> CHW uint8 -> kmodel /255
#
# Differences from det_live_xy.py:
#   - Uses aicube.anchorfreedet_post_process (NOT anchorbasedet_*).
#   - No anchors list in deploy_config.json.
#   - YOLOv8 output is (1, 6, 6300) for 640x480, 2-class; that's
#     [cx, cy, w, h, cls0, cls1] per anchor, classes already sigmoided,
#     bboxes in MODEL-input pixel coords. aicube's anchorfreedet helper
#     does the letterbox inverse for us.
#   - Class-id swap for Teensy wire format:
#         model cls 0 (silver) -> wire 1 (K230_SILVER)
#         model cls 1 (black)  -> wire 0 (K230_BLACK)
#     so the Teensy K230ObjectType enum stays K230_BLACK=0, K230_SILVER=1
#     as it always has.
#
# Wire protocol: identical to det_live_xy.py (8-byte fixed packet):
#   [0xAA] [0x55] [type] [score] [x_hi] [x_lo] [y_hi] [y_lo]
#   type = 0xFF (none), 0 (K230_BLACK), 1 (K230_SILVER)

import os
import gc
import time
import ujson

import nncase_runtime as nn
import ulab.numpy as np
import image

# Decoder is INLINED below. CanMV MicroPython lacks sys.path, so importing
# from a sibling module fails with
# "AttributeError: 'module' object has no attribute 'path'".

from machine import UART, FPIOA
from media.sensor import *
from media.display import *
from media.media import *


NUM_ANCHORS_640x480 = 6300   # 80*60 + 40*30 + 20*15

ROOT_PATH = "/data/k230-train"
DEPLOY_CONFIG_PATH = ROOT_PATH + "/deploy_config.json"


# ---------------------------------------------------------------------------
# Inlined YOLOv8 anchor-free decoder.
# ---------------------------------------------------------------------------
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


def decode_yolov8_anchorfree(flat_out, num_classes, conf_thr,
                              num_anchors=NUM_ANCHORS_640x480):
    N = num_anchors
    off_cx = 0
    off_cy = N
    off_w  = 2 * N
    off_h  = 3 * N
    off_c  = 4 * N
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


def nms_class_wise(boxes, iou_thr):
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


def unletterbox_box(box_xyxy, ratio, left_pad, top_pad, ori_w, ori_h):
    x1 = (box_xyxy[0] - left_pad) / ratio
    y1 = (box_xyxy[1] - top_pad)  / ratio
    x2 = (box_xyxy[2] - left_pad) / ratio
    y2 = (box_xyxy[3] - top_pad)  / ratio
    if x1 < 0: x1 = 0.0
    if y1 < 0: y1 = 0.0
    if x2 > ori_w: x2 = float(ori_w)
    if y2 > ori_h: y2 = float(ori_h)
    return [x1, y1, x2, y2]
# ---------------------------------------------------------------------------

SENSOR_FRAMESIZE = Sensor.VGA
APPLY_HISTEQ     = True     # Matches calibration: training/calib JPGs are histeq'd gray-3ch.

CONF_THRESHOLD   = 0.30
NMS_THRESHOLD    = 0.50
STRIDES          = [8, 16, 32]
NUM_ANCHORS      = NUM_ANCHORS_640x480

UART_DEVICE      = UART.UART1   # change to UART.UART2/UART3 to match your wiring
UART_BAUD        = 115200

SYNC0 = 0xAA
SYNC1 = 0x55
TYPE_NONE = 0xFF

SHOW_DISPLAY = True

# If True, start detecting immediately and draw boxes on the live feed.
# If False (production with a Teensy controller), wait for a 0x01 byte on
# UART RX before running inference.
AUTO_RUN_DETECTION = True

# Per-class colors for drawn boxes (matches det_image_yolov8.py palette).
COLOR_PALETTE = [
    (220, 20, 60), (119, 11, 32), (0, 0, 142), (0, 0, 230),
    (106, 0, 228), (0, 60, 100), (0, 80, 100), (0, 0, 70),
]
DEBUG_EVERY  = 30


def _open_uart():
    try:
        fpioa = FPIOA()
        fpioa.set_function(11, FPIOA.UART1_TXD)
        fpioa.set_function(12, FPIOA.UART1_RXD)
    except Exception as e:
        print("FPIOA setup skipped:", e)
    # CanMV requires the UART class constants for bits/parity/stop. Plain
    # Python values (8, None, 1) cause "TypeError: can't convert NoneType to int"
    # because the binding does `int(parity)` on the keyword argument.
    return UART(UART_DEVICE,
                baudrate=UART_BAUD,
                bits=UART.EIGHTBITS,
                parity=UART.PARITY_NONE,
                stop=UART.STOPBITS_ONE)


def _send_packet(u, type_id, score_u8, x_px, y_px):
    x = max(0, min(65535, int(x_px)))
    y = max(0, min(65535, int(y_px)))
    s = max(0, min(255, int(score_u8)))
    t = type_id & 0xFF
    pkt = bytes([SYNC0, SYNC1, t, s,
                 (x >> 8) & 0xFF, x & 0xFF,
                 (y >> 8) & 0xFF, y & 0xFF])
    u.write(pkt)


def _read_run_state(u, current):
    state = current
    n = u.any()
    if n:
        try:
            data = u.read(n) or b""
        except Exception:
            return current
        for b in data:
            if b == 0x01:
                state = True
            elif b == 0x00:
                state = False
    return state


def load_config():
    with open(DEPLOY_CONFIG_PATH, "r") as f:
        cfg = ujson.load(f)
    if not cfg["kmodel_path"].startswith("/"):
        # CanMV MicroPython has no os.path; do the dirname by hand.
        config_dir = DEPLOY_CONFIG_PATH.rsplit("/", 1)[0] + "/"
        cfg["kmodel_path"] = config_dir + cfg["kmodel_path"]
    print("Loaded deploy_config:", DEPLOY_CONFIG_PATH)
    print("  kmodel:    ", cfg["kmodel_path"])
    print("  img_size:  ", cfg["img_size"])
    print("  categories:", cfg["categories"])
    print("  model_type:", cfg["model_type"])
    if cfg.get("model_type") != "AnchorFreeDet":
        raise ValueError("This script is YOLOv8 anchor-free only.")
    return cfg


def chw_from_grayscale(img):
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


def main():
    cfg = load_config()
    kmodel_path = cfg["kmodel_path"]
    labels      = cfg["categories"]                    # ["silver", "black"]
    img_size    = cfg["img_size"]
    num_classes = cfg["num_classes"]

    # model cls -> wire cls. Model: 0=silver, 1=black. Wire: 0=black, 1=silver.
    MODEL_TO_WIRE = [1, 0]

    u = _open_uart()
    print("UART opened.")
    run_state = AUTO_RUN_DETECTION
    if AUTO_RUN_DETECTION:
        print("AUTO_RUN_DETECTION=True -- skipping UART start gate.")

    sensor = Sensor()
    sensor.reset()
    sensor.set_framesize(SENSOR_FRAMESIZE)
    sensor.set_pixformat(Sensor.GRAYSCALE)
    if SHOW_DISPLAY:
        Display.init(Display.VIRT, sensor.width(), sensor.height(), to_ide=True)
    MediaManager.init()
    sensor.run()
    time.sleep_ms(200)

    sensor_w = sensor.width()
    sensor_h = sensor.height()
    model_w  = img_size[0]
    model_h  = img_size[1]
    print("sensor {}x{} -> model {}x{}".format(sensor_w, sensor_h, model_w, model_h))

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

    frame = 0
    t_window = time.ticks_ms()
    print("Running. Send 0x01 over UART to start detection.")
    try:
        while True:
            run_state = _read_run_state(u, run_state)
            img = sensor.snapshot()
            if APPLY_HISTEQ:
                img.histeq()

            if not run_state:
                if frame % 10 == 0:
                    _send_packet(u, TYPE_NONE, 0, 0, 0)
                if SHOW_DISPLAY:
                    Display.show_image(img)
                frame += 1
                time.sleep_ms(20)
                continue

            chw = chw_from_grayscale(img)
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

            # YOLOv8 anchor-free postprocess. Our Ultralytics ONNX export
            # produces a SINGLE concatenated output (1, 4+nc, 6300) with
            # cx,cy,w,h in MODEL pixel coords + sigmoided cls scores -- NOT
            # the 3 per-stride raw heads that aicube.anchorfreedet_post_process
            # is designed for. We decode in Python (yolov8_decode.py) so the
            # output stays consistent with the PC simulator path and the
            # det_image_yolov8.py diagnostic.
            try:
                if not results:
                    det = []
                else:
                    letter = decode_yolov8_anchorfree(
                        results[0], num_classes, CONF_THRESHOLD,
                        num_anchors=NUM_ANCHORS,
                    )
                    letter = nms_class_wise(letter, NMS_THRESHOLD)
                    det = []
                    for b in letter:
                        xyxy = unletterbox_box(b[2:6], ratio, left, top,
                                                sensor_w, sensor_h)
                        det.append([b[0], b[1],
                                    xyxy[0], xyxy[1], xyxy[2], xyxy[3]])
            except Exception as e:
                if frame < 3:
                    print("postprocess error:", e)
                det = []

            # Find the highest-score detection -- that's the one the
            # Teensy gets over UART (it can only act on one target/frame).
            best = None
            if det:
                for d in det:
                    if best is None or d[1] > best[1]:
                        best = d

            # UART packet -- only the best detection (or "no target").
            if best is not None:
                model_cls = int(best[0])
                wire_cls = MODEL_TO_WIRE[model_cls] if model_cls < len(MODEL_TO_WIRE) else 0xFF
                score = float(best[1])
                cx = int((best[2] + best[4]) / 2)
                cy = int((best[3] + best[5]) / 2)
                _send_packet(u, wire_cls, int(score * 255), cx, cy)
            else:
                _send_packet(u, TYPE_NONE, 0, 0, 0)

            # Display -- draw EVERY detection that survived NMS, colored by
            # class. Matches det_image_yolov8.py exactly. No "best"
            # highlighting; the UART packet above handles that contract.
            if SHOW_DISPLAY and det:
                for d in det:
                    cls_id = int(d[0])
                    score = float(d[1])
                    x1, y1, x2, y2 = d[2], d[3], d[4], d[5]
                    # CanMV requires (R,G,B); (255,) errors with ValueError.
                    color = COLOR_PALETTE[cls_id % len(COLOR_PALETTE)]
                    img.draw_rectangle(int(x1), int(y1),
                                       int(x2 - x1), int(y2 - y1),
                                       color=color, thickness=2)
                    name = labels[cls_id] if cls_id < len(labels) else "c{}".format(cls_id)
                    img.draw_string_advanced(
                        int(x1), max(0, int(y1) - 20), 16,
                        "{} {:.2f}".format(name, score),
                        color=color,
                    )

            if SHOW_DISPLAY:
                Display.show_image(img)

            gc.collect()
            frame += 1
            if frame % DEBUG_EVERY == 0:
                now = time.ticks_ms()
                fps = DEBUG_EVERY * 1000 / max(1, time.ticks_diff(now, t_window))
                n_det = len(det) if det else 0
                top_s = best[1] if best else 0.0
                top_t = MODEL_TO_WIRE[int(best[0])] if best else -1
                print("f={:5d} fps={:5.2f} run={} dets={} wire_cls={} top_s={:.3f}".format(
                    frame, fps, run_state, n_det, top_t, top_s))
                t_window = now

    except KeyboardInterrupt:
        print("\nstopping...")
    finally:
        try: u.deinit()
        except Exception: pass
        if hasattr(kpu, "deinit"):
            kpu.deinit()
        sensor.stop()
        if SHOW_DISPLAY:
            Display.deinit()
        MediaManager.deinit()
        nn.shrink_memory_pool()
        print("done.")


if __name__ == "__main__":
    main()

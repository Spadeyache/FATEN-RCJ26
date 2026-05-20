# K230D Zero / CanMV v1.5-legacy / nncase_runtime 2.9.0
#
# Single-image YOLOv8 anchor-free inference using a kmodel produced by
# convert_kmodel.py with preprocess_mode=raw_01 (Ultralytics export).
#
# Preprocessing path (must match calibration, verified 2026-05-19):
#   The calibration JPGs (data/calibration_74/) are grayscale-histeq images
#   stored as 3-channel JPG -- every pixel has B=G=R (checked with
#   cv2.split + np.array_equal).  cv2.imread on those returns a (H,W,3)
#   BGR array where all three planes carry the same histeq'd gray data;
#   BGR2RGB is a no-op since the channels are identical; CHW uint8 ->
#   kmodel /255.
#
#   On-device with this script:
#     IMAGE_PATH must be an ALREADY-HISTEQ'd grayscale image saved as JPG
#     (3-channel replicated -- the standard training-set format here).
#     image.Image().to_rgb888() decodes it to 3 identical R=G=B channels
#     -> CHW uint8 -> kmodel /255 (baked dequant).  Byte-equivalent to
#     calibration.  Do NOT re-histeq here; the input file is already done.
#
# Mirrors det_image.py (AnchorBaseDet) for forensic comparison, but:
#   - pure-python decode of the single concatenated YOLOv8 output
#     (1, 4+nc, 6300) -- NOT aicube.anchorfreedet_post_process.
#     aicube's anchorfree helper expects 3 separate per-stride raw feature
#     maps that it then DFL-decodes and grid-decodes itself; feeding it our
#     ONNX-decoded tensor reads bbox channels as scores and prints things
#     like "silver 638.50" at the wrong locations.
#   - reads RGB888 from a JPG that's already gray-histeq replicated to 3ch
#
# Copy this file + deploy_config.json + model.kmodel to /data/k230-train/
# on the K230D SD card before running.
#
# deploy_config.json requirements (warned at startup if mismatched):
#   model_type   = "AnchorFreeDet"
#   categories   = order the MODEL was trained on -- Ultralytics data.yaml
#                  says ['silver','black'] so cls 0 = silver, cls 1 = black.
#                  If your deploy_config.json was written by batch_export.py
#                  with the AnchorBase template it will say "AnchorBaseDet"
#                  and may have categories=['black','silver']; edit it.

import os
import gc
import ujson
import time

import nncase_runtime as nn
import ulab.numpy as np
import image


ROOT_PATH          = "/data/k230-train"
DEPLOY_CONFIG_PATH = ROOT_PATH + "/deploy_config.json"
IMAGE_PATH         = ROOT_PATH + "/test.jpg"
RESULT_PATH        = ROOT_PATH + "/det_result.jpg"

CONF_OVERRIDE = 0.30
NMS_OVERRIDE  = None

APPLY_VFLIP = False
DEBUG       = True

STRIDES = [8, 16, 32]
NUM_ANCHORS = 6300  # 80*60 + 40*30 + 20*15 for 640x480 model.

COLOR_PALETTE = [
    (220, 20, 60), (119, 11, 32), (0, 0, 142), (0, 0, 230),
    (106, 0, 228), (0, 60, 100), (0, 80, 100), (0, 0, 70),
]


class ScopedTiming:
    def __init__(self, info=""):
        self.info = info
    def __enter__(self):
        self.t0 = time.time_ns()
        return self
    def __exit__(self, *exc):
        ms = (time.time_ns() - self.t0) / 1e6
        print("{} took {:.2f} ms".format(self.info, ms))


def read_img_rgb888(img_path):
    """Load image as CHW uint8 RGB ulab.numpy array, matching calibration."""
    img_data = image.Image(img_path)
    img_rgb888 = img_data.to_rgb888()
    hwc = img_rgb888.to_numpy_ref()
    if APPLY_VFLIP:
        hwc = hwc[::-1, :, :].copy()
    H, W, C = hwc.shape
    if C != 3:
        raise ValueError("Expected 3-channel RGB888, got C={}".format(C))
    tmp = hwc.reshape((H * W, C)).transpose().copy()
    return tmp.reshape((C, H, W))


def read_deploy_config(path):
    with open(path, "r") as f:
        cfg = ujson.load(f)
    print("Loaded deploy_config.json from", path)
    return cfg


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
                              num_anchors=NUM_ANCHORS):
    """Decode the YOLOv8 single concatenated output (1, 4+nc, num_anchors).

    The kmodel was compiled with output_layout=NCHW so the flattened buffer
    is channel-major: flat[c * num_anchors + n] = channel c, anchor n.
    Channels 0..3 are [cx, cy, w, h] in MODEL letterbox pixel coords (the
    Ultralytics ONNX exports them already decoded from DFL). Channels
    4..3+nc are sigmoided class scores.

    Returns list of [cls, score, x1, y1, x2, y2] in MODEL letterbox coords.
    """
    N = num_anchors
    off_cx = 0
    off_cy = N
    off_w  = 2 * N
    off_h  = 3 * N
    off_c  = 4 * N

    boxes = []
    for n in range(N):
        # argmax + max over cls channels for this anchor
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
    """Per-class greedy NMS. Returns sorted-by-score list."""
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


def unletterbox_box(box, ratio, left_pad, top_pad, ori_w, ori_h):
    """Map a box from MODEL letterbox coords back to original-image coords."""
    x1 = (box[0] - left_pad) / ratio
    y1 = (box[1] - top_pad)  / ratio
    x2 = (box[2] - left_pad) / ratio
    y2 = (box[3] - top_pad)  / ratio
    if x1 < 0: x1 = 0.0
    if y1 < 0: y1 = 0.0
    if x2 > ori_w: x2 = float(ori_w)
    if y2 > ori_h: y2 = float(ori_h)
    return [x1, y1, x2, y2]


def tensor_stats_summary(t, label=""):
    flat = t.reshape((-1,))
    n = min(1000, flat.shape[0])
    sample = flat[:n]
    mn = sample[0]; mx = sample[0]; s = 0
    for i in range(n):
        v = sample[i]
        if v < mn: mn = v
        if v > mx: mx = v
        s += v
    print("  {}: shape={} sample_min={} sample_max={} sample_mean={:.2f}".format(
        label, t.shape, mn, mx, s / n))


def detection():
    print("===== det_image_yolov8.py =====")
    cfg = read_deploy_config(DEPLOY_CONFIG_PATH)

    kmodel_path = cfg["kmodel_path"]
    if not kmodel_path.startswith("/"):
        config_dir = DEPLOY_CONFIG_PATH.rsplit("/", 1)[0] + "/"
        kmodel_path = config_dir + kmodel_path

    labels      = cfg["categories"]
    img_size    = cfg["img_size"]
    num_classes = cfg["num_classes"]
    conf_thr    = CONF_OVERRIDE if CONF_OVERRIDE is not None else cfg["confidence_threshold"]
    nms_thr     = NMS_OVERRIDE  if NMS_OVERRIDE  is not None else cfg["nms_threshold"]
    model_type  = cfg.get("model_type", "")
    if model_type != "AnchorFreeDet":
        print("WARN: deploy_config.json model_type='{}', expected 'AnchorFreeDet'.".format(model_type))
        print("      Continuing -- but edit deploy_config.json so the live "
              "script's check passes.")

    print("kmodel:     ", kmodel_path)
    print("image:      ", IMAGE_PATH)
    print("input W x H:", img_size[0], "x", img_size[1])
    print("classes:    ", labels, "(model trained 0=silver, 1=black)")
    print("conf/nms:   ", conf_thr, "/", nms_thr)
    if "_meta" in cfg:
        print("_meta:      ", cfg["_meta"])

    with ScopedTiming("setup read_img + to CHW (RGB888)"):
        ai2d_input = read_img_rgb888(IMAGE_PATH)
    ori_w = ai2d_input.shape[2]
    ori_h = ai2d_input.shape[1]
    frame_size = [ori_w, ori_h]
    if DEBUG:
        tensor_stats_summary(ai2d_input, "raw input CHW")

    ai2d_input_tensor = nn.from_numpy(ai2d_input)

    model_w = img_size[0]
    model_h = img_size[1]
    ai2d_out = nn.from_numpy(np.ones((1, 3, model_h, model_w), dtype=np.uint8))

    ratio = min(model_w / ori_w, model_h / ori_h)
    new_w = int(ratio * ori_w)
    new_h = int(ratio * ori_h)
    dw = (model_w - new_w) / 2
    dh = (model_h - new_h) / 2
    top    = int(round(dh - 0.1))
    bottom = int(round(dh + 0.1))
    left   = int(round(dw - 0.1))
    right  = int(round(dw + 0.1))

    with ScopedTiming("setup kpu.load_kmodel"):
        kpu = nn.kpu()
        kpu.load_kmodel(kmodel_path)

    with ScopedTiming("setup ai2d.build"):
        ai2d = nn.ai2d()
        ai2d.set_dtype(nn.ai2d_format.NCHW_FMT, nn.ai2d_format.NCHW_FMT,
                       np.uint8, np.uint8)
        ai2d.set_pad_param(True, [0, 0, 0, 0, top, bottom, left, right], 0,
                           [114, 114, 114])
        ai2d.set_resize_param(True, nn.interp_method.tf_bilinear,
                              nn.interp_mode.half_pixel)
        ai2d_builder = ai2d.build([1, 3, ori_h, ori_w],
                                  [1, 3, model_h, model_w])

    with ScopedTiming("TOTAL end-to-end"):
        with ScopedTiming("  stage ai2d preprocess (resize+letterbox)"):
            ai2d_builder.run(ai2d_input_tensor, ai2d_out)
        with ScopedTiming("  stage KPU set_input + run"):
            kpu.set_input_tensor(0, ai2d_out)
            kpu.run()
        del ai2d_input_tensor
        del ai2d_out

        with ScopedTiming("  stage KPU get_output_tensor"):
            results = []
            for i in range(kpu.outputs_size()):
                data = kpu.get_output_tensor(i)
                r = data.to_numpy()
                if DEBUG:
                    print("out[{}] raw shape: {}".format(i, r.shape))
                total = 1
                for s in r.shape:
                    total *= s
                results.append(r.reshape((total,)))
                del data

        with ScopedTiming("  stage gc.collect"):
            gc.collect()

        with ScopedTiming("  stage image.Image load + to_rgb565 (canvas)"):
            image_draw = image.Image(IMAGE_PATH).to_rgb565()

        with ScopedTiming("  stage decode_yolov8_anchorfree"):
            # The Ultralytics YOLOv8 export ends in Concat-of-decoded-boxes +
            # Sigmoid-on-classes, so we get a single tensor (1, 4+nc, N) with
            # cx,cy,w,h in MODEL pixel coords + sigmoided class scores.
            # aicube.anchorfreedet_post_process expects 3 per-stride raw
            # feature maps (it does DFL + grid decoding itself) -- feeding it
            # our already-decoded tensor reads bbox channels as scores and
            # produces gibberish like "silver 638.50".  Decode manually.
            if not results:
                det_boxes_letter = []
            else:
                det_boxes_letter = decode_yolov8_anchorfree(
                    results[0], num_classes, conf_thr,
                    num_anchors=NUM_ANCHORS,
                )
                if DEBUG:
                    print("  decoded pre-NMS: {}".format(len(det_boxes_letter)))

        with ScopedTiming("  stage class-wise NMS"):
            det_boxes_letter = nms_class_wise(det_boxes_letter, nms_thr)
            if DEBUG:
                print("  kept post-NMS: {}".format(len(det_boxes_letter)))

        with ScopedTiming("  stage unletterbox -> original coords"):
            det_boxes = []
            for b in det_boxes_letter:
                xyxy = unletterbox_box(b[2:6], ratio, left, top,
                                        ori_w, ori_h)
                det_boxes.append([b[0], b[1],
                                  xyxy[0], xyxy[1], xyxy[2], xyxy[3]])

        with ScopedTiming("  stage draw boxes on image_draw"):
            if det_boxes:
                print("Detections:", len(det_boxes))
                try:
                    det_boxes = sorted(det_boxes, key=lambda d: -d[1])
                except Exception:
                    pass
                img_h = image_draw.height()
                for d in det_boxes:
                    cls_id = int(d[0])
                    score = float(d[1])
                    x1, y1, x2, y2 = d[2], d[3], d[4], d[5]
                    if APPLY_VFLIP:
                        y1, y2 = img_h - y2, img_h - y1
                    w = float(x2 - x1)
                    h = float(y2 - y1)
                    color = COLOR_PALETTE[cls_id % len(COLOR_PALETTE)]
                    image_draw.draw_rectangle(int(x1), int(y1), int(w), int(h),
                                              color=color)
                    name = labels[cls_id] if cls_id < len(labels) else "c{}".format(cls_id)
                    image_draw.draw_string_advanced(
                        int(x1), max(0, int(y1) - 50), 20,
                        "{} {:.2f}".format(name, score), color=color,
                    )
                    cx = int((x1 + x2) / 2)
                    cy = int((y1 + y2) / 2)
                    print("  {:>7s} {:.3f}  box=({:>4d},{:>4d})-({:>4d},{:>4d})  center=({},{})  size={}x{}".format(
                        name, score, int(x1), int(y1), int(x2), int(y2),
                        cx, cy, int(w), int(h)))
            else:
                print("No objects above conf={}".format(conf_thr))

        if det_boxes:
            with ScopedTiming("  stage image_draw.compress_for_ide()"):
                image_draw.compress_for_ide()
            with ScopedTiming("  stage image_draw.save(RESULT_PATH)"):
                image_draw.save(RESULT_PATH)
            print("Saved annotated ->", RESULT_PATH)

        del results
        del ai2d
        del ai2d_builder
        del kpu
        gc.collect()

    print("===== end =====")
    nn.shrink_memory_pool()


if __name__ == "__main__":
    nn.shrink_memory_pool()
    detection()

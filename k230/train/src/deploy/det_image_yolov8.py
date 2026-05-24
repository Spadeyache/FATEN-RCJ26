# K230D Zero / CanMV v1.5-legacy / nncase_runtime 2.9.0
#
# Single-image YOLOv8 anchor-free inference using a kmodel produced by
# convert_kmodel.py with preprocess_mode=raw_01 (Ultralytics export).
#
# Preprocessing path (must match calibration, verified 2026-05-19):
#   The calibration JPGs (data/calibration_74/) are grayscale-histeq images
#   stored as 3-channel JPG -- every pixel has B=G=R (checked with
#   cv2.split + np.array_equal).
#
#   On-device this script reproduces that pipeline regardless of what the
#   input JPG actually is:
#     image.Image(IMAGE_PATH)
#       -> .to_grayscale()        # collapse to 1 channel
#       -> .histeq()              # match the training/live-camera path
#       -> replicate to (3, H, W) # match calibration's 3-channel format
#       -> ai2d letterbox-114 -> CHW uint8 -> kmodel /255
#   This is byte-equivalent to what the live grayscale-sensor pipeline does
#   and to what cv2.imread + BGR2RGB did during calibration.  If the input
#   JPG is already gray-histeq, the second histeq is a near-no-op on an
#   already-equalized histogram; if the input is a color photo, this is the
#   conversion that brings it into the trained distribution.
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

# Shared decoder lives in the same directory on the K230D SD card.
from yolov8_decode import (
    decode_yolov8_anchorfree, nms_class_wise, unletterbox_box,
    NUM_ANCHORS_640x480,
)


ROOT_PATH          = "/data/k230-train"
DEPLOY_CONFIG_PATH = ROOT_PATH + "/deploy_config.json"
IMAGE_PATH         = ROOT_PATH + "/test.jpg"
RESULT_PATH        = ROOT_PATH + "/det_result.jpg"

CONF_OVERRIDE = 0.30
NMS_OVERRIDE  = None

APPLY_VFLIP = False
DEBUG       = True

STRIDES = [8, 16, 32]
NUM_ANCHORS = NUM_ANCHORS_640x480

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


def read_img_gray_histeq_3ch(img_path):
    """Load JPG -> grayscale -> histeq -> replicate to (3, H, W) uint8.

    Matches the live grayscale-sensor path exactly: sensor.snapshot() ->
    img.histeq() -> chw_from_grayscale().  Also matches the calibration
    distribution (gray-histeq replicated to 3 channels, R=G=B).
    """
    img_data = image.Image(img_path)
    gray = img_data.to_grayscale()
    gray.histeq()
    hwc = gray.to_numpy_ref()
    shape = hwc.shape
    if len(shape) == 2:
        H, W = shape
        plane = hwc
    elif len(shape) == 3 and shape[2] == 1:
        H, W, _ = shape
        plane = hwc[:, :, 0]
    else:
        raise ValueError("Unexpected grayscale shape: {}".format(shape))
    if APPLY_VFLIP:
        plane = plane[::-1, :].copy()
    chw = np.zeros((3, H, W), dtype=np.uint8)
    chw[0] = plane
    chw[1] = plane
    chw[2] = plane
    return chw


def read_deploy_config(path):
    with open(path, "r") as f:
        cfg = ujson.load(f)
    print("Loaded deploy_config.json from", path)
    return cfg


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

    with ScopedTiming("setup read_img gray->histeq->3ch"):
        ai2d_input = read_img_gray_histeq_3ch(IMAGE_PATH)
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

# K230D Zero / CanMV v1.5-legacy / nncase_runtime 2.9.0
#
# Single-image AnchorBaseDet inference using a kmodel produced by our
# explicit conversion pipeline. Reads its own deploy_config.json so anchors /
# categories / img_size / thresholds come from the variant being tested
# rather than being hardcoded.
#
# Based on the verified-working k230-inference/Yolov8nimg_aicube.py.
# Key differences from the AI Cube sample:
#   - APPLY_VFLIP defaults to False (this branch's reconstructed model is
#     trained on un-flipped data, unlike AI Cube's training set).
#   - Verbose debug prints of input tensor stats and top decoded boxes so
#     you can compare to the PC evaluator.
#
# Copy this file + deploy_config.json + model.kmodel to /data/k230-train/
# on the K230D SD card before running.

import os
import gc
import ujson
import time

import nncase_runtime as nn
import ulab.numpy as np
import aicube
import image


# ---------------------------------------------------------------------------
# Paths (edit per board)
# ---------------------------------------------------------------------------
ROOT_PATH          = "/data/k230-train"
DEPLOY_CONFIG_PATH = ROOT_PATH + "/deploy_config.json"
IMAGE_PATH         = ROOT_PATH + "/test.jpg"
RESULT_PATH        = ROOT_PATH + "/det_result.jpg"

# Thresholds override (None = use whatever deploy_config.json says).
CONF_OVERRIDE = 0.2
NMS_OVERRIDE  = None

APPLY_VFLIP   = False     # set True if camera orientation needs flipping
DEBUG         = True      # print extensive debug info

STRIDES = [8, 16, 32]

# Color palette for drawing
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


def read_img(img_path):
    """Load image and return CHW uint8 ulab.numpy array (3 channels)."""
    img_data = image.Image(img_path)
    img_rgb888 = img_data.to_rgb888()
    hwc = img_rgb888.to_numpy_ref()
    if APPLY_VFLIP:
        hwc = hwc[::-1, :, :].copy()
    H, W, C = hwc.shape
    tmp = hwc.reshape((H * W, C)).transpose().copy()
    return tmp.reshape((C, H, W))


def read_deploy_config(path):
    with open(path, "r") as f:
        cfg = ujson.load(f)
    print("Loaded deploy_config.json from", path)
    return cfg


def tensor_stats_summary(t, label=""):
    """Print min/max/mean of a small sample for debug."""
    flat = t.reshape((-1,))
    n = min(1000, flat.shape[0])
    sample = flat[:n]
    # ulab.numpy doesn't have .min/.max as floats reliably across all versions;
    # compute manually:
    mn = sample[0]
    mx = sample[0]
    s = 0
    for i in range(n):
        v = sample[i]
        if v < mn:
            mn = v
        if v > mx:
            mx = v
        s += v
    print("  {}: shape={} sample_min={} sample_max={} sample_mean={:.2f}".format(
        label, t.shape, mn, mx, s / n))


def detection():
    print("===== det_image.py =====")
    cfg = read_deploy_config(DEPLOY_CONFIG_PATH)
    kmodel_path = cfg["kmodel_path"]
    if not kmodel_path.startswith("/"):
        config_dir = DEPLOY_CONFIG_PATH.rsplit("/", 1)[0] + "/"
        kmodel_path = config_dir + kmodel_path
        print("Resolved kmodel ->", kmodel_path)

    labels      = cfg["categories"]
    img_size    = cfg["img_size"]                 # [W, H]
    num_classes = cfg["num_classes"]
    conf_thr    = CONF_OVERRIDE if CONF_OVERRIDE is not None else cfg["confidence_threshold"]
    nms_thr     = NMS_OVERRIDE  if NMS_OVERRIDE  is not None else cfg["nms_threshold"]
    nms_option  = cfg["nms_option"]
    model_type  = cfg["model_type"]
    if model_type != "AnchorBaseDet":
        raise ValueError("This script supports AnchorBaseDet only, got: " + model_type)

    a = cfg["anchors"]
    anchors_flat = a[0] + a[1] + a[2]

    print("kmodel:     ", kmodel_path)
    print("image:      ", IMAGE_PATH)
    print("input W x H:", img_size[0], "x", img_size[1])
    print("classes:    ", labels)
    print("conf/nms:   ", conf_thr, "/", nms_thr)
    if "_meta" in cfg:
        print("_meta:      ", cfg["_meta"])

    # --- one-time setup (counts toward end-to-end but not per-frame loop) ---
    with ScopedTiming("setup read_img + to CHW"):
        ai2d_input = read_img(IMAGE_PATH)
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

    # Per-stage profiling so we can see where the 59 s end-to-end went.
    # Each stage is timed independently. Run once; the first run includes
    # JIT/cache warm-up so a second run will be faster on KPU-bound stages.
    with ScopedTiming("TOTAL end-to-end"):
        with ScopedTiming("  stage ai2d preprocess (resize+letterbox)"):
            ai2d_builder.run(ai2d_input_tensor, ai2d_out)
        with ScopedTiming("  stage KPU set_input + run"):
            kpu.set_input_tensor(0, ai2d_out)
            kpu.run()
        del ai2d_input_tensor
        del ai2d_out

        with ScopedTiming("  stage KPU get_output_tensor x3"):
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

        with ScopedTiming("  stage image.Image load + to_rgb565 (for draw canvas)"):
            image_draw = image.Image(IMAGE_PATH).to_rgb565()

        with ScopedTiming("  stage aicube.anchorbasedet_post_process"):
            if len(results) >= 3:
                det_boxes = aicube.anchorbasedet_post_process(
                    results[0], results[1], results[2],
                    img_size, frame_size, STRIDES,
                    num_classes, conf_thr, nms_thr,
                    anchors_flat, nms_option,
                )
            else:
                det_boxes = aicube.anchorbasedet_post_process(
                    results[0], results[0], results[0],
                    img_size, frame_size, [STRIDES[0]] * 3,
                    num_classes, conf_thr, nms_thr,
                    anchors_flat, nms_option,
                )

        with ScopedTiming("  stage draw boxes on image_draw"):
            if det_boxes:
                print("Detections:", len(det_boxes))
                try:
                    det_boxes = sorted(det_boxes, key=lambda d: -d[1])
                except Exception:
                    pass
                img_h = image_draw.height()
                for d in det_boxes:
                    cls_id = d[0]
                    score = d[1]
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

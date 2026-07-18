# Yolov8nimg_aicube.py
# Single-image test for an AI Cube anchor-based kmodel.
#
# Follows AI Cube's official det_image.py sample exactly:
#   read JPG -> CHW -> ai2d (resize + letterbox) -> kpu -> aicube postprocess
#   -> draw boxes -> save annotated jpg
#
# Use this to verify the kmodel works against your dataset, separate from
# the live camera path. If detections land on the right objects here, the
# model is good and live issues are camera/preprocess specific.

import os, gc, ujson
import nncase_runtime as nn
import ulab.numpy as np
import aicube
import image
import time


# ---------------------------------------------------------------------------
# Config — overridden by deploy_config.json if present
# ---------------------------------------------------------------------------
# Path to AI Cube's deploy_config.json. If found, all settings below are
# replaced by what's in the JSON.
DEPLOY_CONFIG_PATH = "/data/kmodel/deploy_config.json"

# Test image to run inference on (any image from your dataset works)
IMAGE_PATH  = "/data/dataset/1s0b/1s0b_0010.jpg"

# Where to save the annotated result
RESULT_PATH = "/data/det_result.jpg"

# Override the threshold from deploy_config.json. Set to None to use JSON's value.
CONF_OVERRIDE = 0.3
NMS_OVERRIDE  = None    # None keeps JSON's value

# K230 deployment fixes (diagnosed by diag_raw_output.py):
#  - Image needs vertical flip to match AI Cube training orientation.
#  - Class label order in deploy_config.json is swapped vs how AI Cube trained.
APPLY_VFLIP    = False
LABELS_SWAP    = ["black", "silver"]   # actual order the model learned (cls0, cls1)

# Hard-coded fallbacks used only if deploy_config.json isn't found.
# IMPORTANT: replace anchors with the values from your AI Cube deploy_config.json
# if you want correct box positions.
FALLBACK = {
    "kmodel_path":          "/data/kmodel/best_AnchorBaseDet_can3_5_n_20260514232500.kmodel",
    "categories":           ["black", "silver"],
    "img_size":             [640, 480],          # [W, H] model input
    "num_classes":          2,
    "confidence_threshold": 0.1,
    "nms_threshold":        0.45,
    "nms_option":           False,
    "model_type":           "AnchorBaseDet",
    "anchors": [
        [10, 13, 16, 30, 33, 23],
        [30, 61, 62, 45, 59, 119],
        [116, 90, 156, 198, 373, 326],
    ],
}

STRIDES = [8, 16, 32]

color_three = [
    (220, 20, 60), (119, 11, 32), (0, 0, 142), (0, 0, 230),
    (106, 0, 228), (0, 60, 100), (0, 80, 100), (0, 0, 70),
]


# ---------------------------------------------------------------------------
# Helpers (mirror AI Cube sample)
# ---------------------------------------------------------------------------
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
    """
    Load an image and return as CHW uint8 ulab.numpy array, 3 channels.

    Works for color JPGs and grayscale JPGs (which decode to 3 equal channels
    via to_rgb888()).
    """
    img_data = image.Image(img_path)
    img_rgb888 = img_data.to_rgb888()
    hwc = img_rgb888.to_numpy_ref()
    if APPLY_VFLIP:
        hwc = hwc[::-1, :, :].copy()
    H, W, C = hwc.shape
    tmp = hwc.reshape((H * W, C)).transpose().copy()
    return tmp.reshape((C, H, W))


def read_deploy_config(path):
    try:
        with open(path, "r") as f:
            cfg = ujson.load(f)
        print("Loaded deploy_config.json from", path)
        return cfg
    except Exception as e:
        print("No deploy_config.json ({}). Using FALLBACK.".format(e))
        return FALLBACK


# ---------------------------------------------------------------------------
# Detection
# ---------------------------------------------------------------------------
def detection():
    print("-------------- start -----------------")

    cfg = read_deploy_config(DEPLOY_CONFIG_PATH)
    kmodel_path  = cfg["kmodel_path"]
    # AI Cube's deploy_config.json typically stores kmodel_path as a filename
    # only (no directory). Resolve it relative to the config file's dir.
    if not kmodel_path.startswith("/"):
        config_dir = DEPLOY_CONFIG_PATH.rsplit("/", 1)[0] + "/"
        kmodel_path = config_dir + kmodel_path
        print("Resolved kmodel path ->", kmodel_path)
    labels       = cfg["categories"]
    img_size     = cfg["img_size"]              # [W, H]
    num_classes  = cfg["num_classes"]
    conf_thr     = CONF_OVERRIDE if CONF_OVERRIDE is not None else cfg["confidence_threshold"]
    nms_thr      = NMS_OVERRIDE  if NMS_OVERRIDE  is not None else cfg["nms_threshold"]
    nms_option   = cfg["nms_option"]
    model_type   = cfg["model_type"]

    if model_type == "AnchorBaseDet":
        a = cfg["anchors"]
        anchors_flat = a[0] + a[1] + a[2]
    else:
        anchors_flat = []

    print("Model       :", kmodel_path)
    print("Image       :", IMAGE_PATH)
    print("Input  W x H:", img_size[0], "x", img_size[1])
    print("Classes     :", labels)
    print("Conf / NMS  :", conf_thr, "/", nms_thr)
    print("Model type  :", model_type)

    # ----- load image as CHW -----
    ai2d_input = read_img(IMAGE_PATH)
    frame_size = [ai2d_input.shape[2], ai2d_input.shape[1]]   # [W, H] original
    ai2d_input_tensor = nn.from_numpy(ai2d_input)

    # ----- allocate ai2d output tensor (model input size) -----
    model_w = img_size[0]
    model_h = img_size[1]
    ai2d_out = nn.from_numpy(np.ones((1, 3, model_h, model_w), dtype=np.uint8))

    # ----- compute letterbox padding -----
    ori_w = ai2d_input.shape[2]
    ori_h = ai2d_input.shape[1]
    ratio = min(model_w / ori_w, model_h / ori_h)
    new_w = int(ratio * ori_w)
    new_h = int(ratio * ori_h)
    dw = (model_w - new_w) / 2
    dh = (model_h - new_h) / 2
    top    = int(round(dh - 0.1))
    bottom = int(round(dh + 0.1))
    left   = int(round(dw - 0.1))
    right  = int(round(dw + 0.1))

    # ----- KPU + ai2d -----
    kpu = nn.kpu()
    kpu.load_kmodel(kmodel_path)

    ai2d = nn.ai2d()
    ai2d.set_dtype(nn.ai2d_format.NCHW_FMT, nn.ai2d_format.NCHW_FMT,
                   np.uint8, np.uint8)
    ai2d.set_pad_param(True, [0, 0, 0, 0, top, bottom, left, right], 0, [114, 114, 114])
    ai2d.set_resize_param(True, nn.interp_method.tf_bilinear, nn.interp_mode.half_pixel)
    ai2d_builder = ai2d.build([1, 3, ori_h, ori_w], [1, 3, model_h, model_w])

    with ScopedTiming("total"):
        # preprocess
        ai2d_builder.run(ai2d_input_tensor, ai2d_out)

        # inference
        kpu.set_input_tensor(0, ai2d_out)
        kpu.run()
        del ai2d_input_tensor
        del ai2d_out

        # gather outputs (1 or 3 depending on AI Cube head config)
        results = []
        for i in range(kpu.outputs_size()):
            data = kpu.get_output_tensor(i)
            r = data.to_numpy()
            total = 1
            for s in r.shape:
                total *= s
            results.append(r.reshape((total,)))
            del data

        print("Output tensor count:", len(results))

        gc.collect()

        # canvas to draw on (RGB565 for save)
        image_draw = image.Image(IMAGE_PATH).to_rgb565()

        # postprocess
        if model_type == "AnchorBaseDet":
            if len(results) >= 3:
                det_boxes = aicube.anchorbasedet_post_process(
                    results[0], results[1], results[2],
                    img_size, frame_size, STRIDES,
                    num_classes, conf_thr, nms_thr,
                    anchors_flat, nms_option,
                )
            else:
                # Single-head model (only stride 8)
                det_boxes = aicube.anchorbasedet_post_process(
                    results[0], results[0], results[0],
                    img_size, frame_size, [STRIDES[0]] * 3,
                    num_classes, conf_thr, nms_thr,
                    anchors_flat, nms_option,
                )
        elif model_type == "GFLDet":
            det_boxes = aicube.gfldet_post_process(
                results[0], results[1], results[2],
                img_size, frame_size, STRIDES,
                num_classes, conf_thr, nms_thr, nms_option,
            )
        else:
            det_boxes = aicube.anchorfreedet_post_process(
                results[0], results[1], results[2],
                img_size, frame_size, STRIDES,
                num_classes, conf_thr, nms_thr, nms_option,
            )

        if det_boxes:
            print("Detections:", len(det_boxes))
            # Sort by score for easier reading
            try:
                det_boxes = sorted(det_boxes, key=lambda d: -d[1])
            except Exception:
                pass
            # Coordinates from postprocess are in the FLIPPED frame.
            # Flip Y back so they land on the un-flipped image_draw.
            img_h = image_draw.height()
            for d in det_boxes:
                cls_id, score, x1, y1, x2, y2 = d[0], d[1], d[2], d[3], d[4], d[5]
                if APPLY_VFLIP:
                    y1, y2 = img_h - y2, img_h - y1
                w = float(x2 - x1)
                h = float(y2 - y1)
                color = color_three[cls_id % len(color_three)]
                image_draw.draw_rectangle(int(x1), int(y1), int(w), int(h), color=color)
                name = LABELS_SWAP[cls_id] if cls_id < len(LABELS_SWAP) else "c{}".format(cls_id)
                image_draw.draw_string_advanced(
                    int(x1), max(0, int(y1) - 50), 20,
                    "{} {:.2f}".format(name, score), color=color,
                )
                cx = int((x1 + x2) / 2)
                cy = int((y1 + y2) / 2)
                print("  {:>7s} {:.3f}  box=({:>4d},{:>4d})-({:>4d},{:>4d})  center=({},{})  size={}x{}".format(
                    name, score, int(x1), int(y1), int(x2), int(y2), cx, cy, int(w), int(h)))

            image_draw.compress_for_ide()
            image_draw.save(RESULT_PATH)
            print("Saved annotated result ->", RESULT_PATH)
        else:
            print("No objects detected above conf={}".format(conf_thr))

        del results
        del ai2d
        del ai2d_builder
        del kpu
        gc.collect()

    print("--------------- end ------------------")
    nn.shrink_memory_pool()


if __name__ == "__main__":
    nn.shrink_memory_pool()
    detection()

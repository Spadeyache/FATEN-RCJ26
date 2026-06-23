# ============================================================================
# deploy_canmv_yolov8.py  --  K230D Zero / CanMV / nncase_runtime 2.9.0
#
# On-device single-image YOLOv8 inference for a kmodel built by
# k230_pipeline.py (RGB DIRECT-RESIZE, no letterbox).
#
# Preprocessing MUST match the kmodel calibration exactly:
#   RGB, direct resize to (W,H), uint8 [0,255], NCHW.
#   The /255 (input_range[0,1], std[1,1,1]) is baked into the kmodel, so we
#   feed raw uint8 [0,255] — ai2d only resizes here, NO pad/letterbox.
#
# Bundle onto the K230D SD card at /data/k230-final-train/:
#   best.kmodel  +  labels.txt  +  deploy_config.json  +  this script.
# Then from the CanMV REPL:   import deploy_canmv_yolov8; deploy_canmv_yolov8.detection()
# ============================================================================

import os
import gc
import time
import ujson

import nncase_runtime as nn
import ulab.numpy as np
import image

ROOT_PATH          = "/data/k230-final-train"
DEPLOY_CONFIG_PATH = ROOT_PATH + "/deploy_config.json"
IMAGE_PATH         = ROOT_PATH + "/test.jpg"
RESULT_PATH        = ROOT_PATH + "/det_result.jpg"

CONF_OVERRIDE = None   # None -> use deploy_config.json confidence_threshold
NMS_OVERRIDE  = None   # None -> use deploy_config.json nms_threshold
DEBUG         = True

COLOR_PALETTE = [
    (220, 20, 60), (0, 0, 142), (0, 200, 0), (119, 11, 32),
    (0, 0, 230), (106, 0, 228), (0, 60, 100), (0, 80, 100),
]


class ScopedTiming:
    def __init__(self, info=""):
        self.info = info
    def __enter__(self):
        self.t0 = time.time_ns(); return self
    def __exit__(self, *exc):
        print("{} took {:.2f} ms".format(self.info, (time.time_ns() - self.t0) / 1e6))


# ---------------------------------------------------------------------------
# YOLOv8 anchor-free decode of the single concatenated output (1, 4+nc, N).
# Channel-major flat buffer: flat[c*N + n]. Coords are MODEL pixel coords.
# ---------------------------------------------------------------------------
def decode_yolov8(flat, n_anchors, num_classes, conf_thr):
    N = n_anchors
    off_cx, off_cy, off_w, off_h, off_c = 0, N, 2 * N, 3 * N, 4 * N
    boxes = []
    for n in range(N):
        best_c = 0
        best_s = flat[off_c + n]
        for c in range(1, num_classes):
            s = flat[off_c + c * N + n]
            if s > best_s:
                best_s = s; best_c = c
        if best_s < conf_thr:
            continue
        cx = flat[off_cx + n]; cy = flat[off_cy + n]
        bw = flat[off_w + n];  bh = flat[off_h + n]
        boxes.append([best_c, float(best_s),
                      float(cx - bw / 2), float(cy - bh / 2),
                      float(cx + bw / 2), float(cy + bh / 2)])
    return boxes


def iou_xyxy(a, b):
    ix1 = a[0] if a[0] > b[0] else b[0]
    iy1 = a[1] if a[1] > b[1] else b[1]
    ix2 = a[2] if a[2] < b[2] else b[2]
    iy2 = a[3] if a[3] < b[3] else b[3]
    iw = ix2 - ix1; ih = iy2 - iy1
    if iw <= 0 or ih <= 0:
        return 0.0
    inter = iw * ih
    union = (a[2]-a[0])*(a[3]-a[1]) + (b[2]-b[0])*(b[3]-b[1]) - inter
    return inter / union if union > 0 else 0.0


def nms_class_wise(boxes, iou_thr):
    kept = []
    for c in set(b[0] for b in boxes):
        cb = [b for b in boxes if b[0] == c]
        cb.sort(key=lambda b: -b[1])
        while cb:
            head = cb.pop(0)
            kept.append(head)
            cb = [b for b in cb if iou_xyxy(head[2:6], b[2:6]) < iou_thr]
    kept.sort(key=lambda b: -b[1])
    return kept


def read_deploy_config(path):
    with open(path, "r") as f:
        return ujson.load(f)


def detection():
    print("===== deploy_canmv_yolov8.py =====")
    cfg = read_deploy_config(DEPLOY_CONFIG_PATH)
    kmodel_path = cfg["kmodel_path"]
    if not kmodel_path.startswith("/"):
        kmodel_path = DEPLOY_CONFIG_PATH.rsplit("/", 1)[0] + "/" + kmodel_path
    labels      = cfg["categories"]
    num_classes = cfg["num_classes"]
    model_w, model_h = cfg["img_size"][0], cfg["img_size"][1]
    conf_thr = CONF_OVERRIDE if CONF_OVERRIDE is not None else cfg["confidence_threshold"]
    nms_thr  = NMS_OVERRIDE  if NMS_OVERRIDE  is not None else cfg["nms_threshold"]

    print("kmodel:", kmodel_path, "| in WxH:", model_w, "x", model_h,
          "| classes:", labels, "| conf/nms:", conf_thr, "/", nms_thr)

    # Load source image as RGB888 -> CHW uint8 (direct, no letterbox/gray).
    img_data = image.Image(IMAGE_PATH).to_rgb888()
    hwc = img_data.to_numpy_ref()          # (H, W, 3) uint8, RGB
    ori_h, ori_w = hwc.shape[0], hwc.shape[1]
    chw = np.zeros((1, 3, ori_h, ori_w), dtype=np.uint8)
    chw[0, 0] = hwc[:, :, 0]
    chw[0, 1] = hwc[:, :, 1]
    chw[0, 2] = hwc[:, :, 2]
    ai2d_in = nn.from_numpy(chw)
    ai2d_out = nn.from_numpy(np.ones((1, 3, model_h, model_w), dtype=np.uint8))

    with ScopedTiming("load_kmodel"):
        kpu = nn.kpu()
        kpu.load_kmodel(kmodel_path)

    with ScopedTiming("ai2d build (direct resize)"):
        ai2d = nn.ai2d()
        ai2d.set_dtype(nn.ai2d_format.NCHW_FMT, nn.ai2d_format.NCHW_FMT,
                       np.uint8, np.uint8)
        # DIRECT resize to match calibration (no pad/letterbox).
        ai2d.set_resize_param(True, nn.interp_method.tf_bilinear,
                              nn.interp_mode.half_pixel)
        ai2d_builder = ai2d.build([1, 3, ori_h, ori_w],
                                  [1, 3, model_h, model_w])

    with ScopedTiming("TOTAL inference"):
        ai2d_builder.run(ai2d_in, ai2d_out)
        kpu.set_input_tensor(0, ai2d_out)
        kpu.run()
        out = kpu.get_output_tensor(0)
        r = out.to_numpy()
        if DEBUG:
            print("raw out shape:", r.shape)
        # flatten channel-major; N = last dim (anchors)
        n_anchors = r.shape[-1]
        total = 1
        for s in r.shape:
            total *= s
        flat = r.reshape((total,))
        del out
        gc.collect()

        boxes = decode_yolov8(flat, n_anchors, num_classes, conf_thr)
        boxes = nms_class_wise(boxes, nms_thr)

    # map model coords -> original image coords (direct resize: scale x/y)
    sx = ori_w / float(model_w)
    sy = ori_h / float(model_h)
    canvas = image.Image(IMAGE_PATH).to_rgb565()
    if boxes:
        print("Detections:", len(boxes))
        for b in boxes:
            cls_id, score = int(b[0]), float(b[1])
            x1 = int(b[2] * sx); y1 = int(b[3] * sy)
            x2 = int(b[4] * sx); y2 = int(b[5] * sy)
            color = COLOR_PALETTE[cls_id % len(COLOR_PALETTE)]
            canvas.draw_rectangle(x1, y1, x2 - x1, y2 - y1, color=color)
            name = labels[cls_id] if cls_id < len(labels) else "c{}".format(cls_id)
            canvas.draw_string_advanced(x1, max(0, y1 - 50), 20,
                                        "{} {:.2f}".format(name, score), color=color)
            print("  {:>8s} {:.3f}  ({},{})-({},{})".format(name, score, x1, y1, x2, y2))
        canvas.compress_for_ide()
        canvas.save(RESULT_PATH)
        print("Saved ->", RESULT_PATH)
    else:
        print("No objects above conf =", conf_thr)

    del kpu, ai2d, ai2d_builder, ai2d_in, ai2d_out
    gc.collect()
    nn.shrink_memory_pool()
    print("===== end =====")


if __name__ == "__main__":
    nn.shrink_memory_pool()
    detection()

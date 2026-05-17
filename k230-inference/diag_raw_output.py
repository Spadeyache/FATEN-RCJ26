# diag_raw_output.py
# On-device diagnostic: bypass aicube postprocess and inspect the kmodel's
# raw output directly. If the raw output is healthy here (matches what the
# Docker simulator showed), aicube postprocess is the bug, not the kmodel.

import os, gc, ujson
import nncase_runtime as nn
import ulab.numpy as np
import image
import time


KMODEL_PATH = "/data/kmodel/best_AnchorBaseDet_can3_5_n_20260514232500.kmodel"
IMAGE_PATH  = "/data/dataset/1s0b/1s0b_0010.jpg"
MODEL_W, MODEL_H = 640, 480

# K230D image orientation differs from AI Cube training set: needs vertical flip.
VERTICAL_FLIP   = True
HORIZONTAL_FLIP = False

# AI Cube trained with classes in opposite order from what deploy_config.json
# says. Use this order for human-readable labels (cls0 -> silver, cls1 -> black).
LABELS_MODEL_ORDER = ["silver", "black"]

print("--- raw output diagnostic ---")
print("Image :", IMAGE_PATH)
print("Model :", KMODEL_PATH)


def sigmoid_scalar(x):
    if x >= 0:
        z = 2.71828 ** (-x)
        return 1.0 / (1.0 + z)
    z = 2.71828 ** x
    return z / (1.0 + z)


# --- Load image, convert to CHW uint8 ---
img_data = image.Image(IMAGE_PATH)
img_rgb  = img_data.to_rgb888()
hwc      = img_rgb.to_numpy_ref()
H, W, C  = hwc.shape
print("Loaded image: {}x{}x{}".format(H, W, C))

# Check channel order — print top-left pixel
print("Top-left pixel  (HWC, expect R=G=B for grayscale JPG): {}".format(list(hwc[0, 0])))

# Apply test flips
if VERTICAL_FLIP:
    hwc = hwc[::-1, :, :].copy()
    print("Applied VERTICAL flip")
if HORIZONTAL_FLIP:
    hwc = hwc[:, ::-1, :].copy()
    print("Applied HORIZONTAL flip")

chw = hwc.reshape((H * W, C)).transpose().copy().reshape((C, H, W))
ai2d_input = nn.from_numpy(chw)

# --- ai2d resize/letterbox ---
ratio = min(MODEL_W / W, MODEL_H / H)
new_w = int(ratio * W)
new_h = int(ratio * H)
dw, dh = (MODEL_W - new_w) / 2, (MODEL_H - new_h) / 2
top    = int(round(dh - 0.1))
bottom = int(round(dh + 0.1))
left   = int(round(dw - 0.1))
right  = int(round(dw + 0.1))

ai2d = nn.ai2d()
ai2d.set_dtype(nn.ai2d_format.NCHW_FMT, nn.ai2d_format.NCHW_FMT,
               np.uint8, np.uint8)
ai2d.set_pad_param(True, [0, 0, 0, 0, top, bottom, left, right], 0, [114, 114, 114])
ai2d.set_resize_param(True, nn.interp_method.tf_bilinear, nn.interp_mode.half_pixel)
ai2d_builder = ai2d.build([1, 3, H, W], [1, 3, MODEL_H, MODEL_W])

ai2d_out = nn.from_numpy(np.ones((1, 3, MODEL_H, MODEL_W), dtype=np.uint8))
ai2d_builder.run(ai2d_input, ai2d_out)

# --- KPU inference ---
kpu = nn.kpu()
kpu.load_kmodel(KMODEL_PATH)
kpu.set_input_tensor(0, ai2d_out)
kpu.run()

# --- Inspect each output ---
print("\nOutput tensors:")
for i in range(kpu.outputs_size()):
    arr = kpu.get_output_tensor(i).to_numpy()
    print("  output[{}] shape={}  min={:.3f}  max={:.3f}".format(
        i, arr.shape, float(np.min(arr)), float(np.max(arr))))

# --- Decode stride-8 head manually (output[0]) ---
print("\nDecoding stride-8 head (output[0]) manually:")
out0 = kpu.get_output_tensor(0).to_numpy()   # (1, 60, 80, 21)
# Reshape to (H, W, anchor, channel)
grid = out0.reshape((60, 80, 3, 7))

# Find top anchor positions by combined score = sigmoid(obj) * max(cls)
best_score = -1.0
best_loc = None
top_list = []
for r in range(60):
    for c in range(80):
        for a in range(3):
            v = grid[r, c, a]
            obj = float(v[4])
            obj_s = sigmoid_scalar(obj)
            cls0  = float(v[5])
            cls1  = float(v[6])
            cls_max = max(cls0, cls1)
            score = obj_s * cls_max
            if score > 0.2:
                top_list.append((score, r, c, a, cls0, cls1, float(v[0]), float(v[1]), float(v[2]), float(v[3])))
            if score > best_score:
                best_score = score
                best_loc = (r, c, a, cls0, cls1, float(v[0]), float(v[1]), float(v[2]), float(v[3]))

# Sort and show top 5
top_list.sort(key=lambda x: -x[0])
print("\nTop detections above 0.2 (max 5 shown):")
for entry in top_list[:5]:
    score, r, c, a, cls0, cls1, rx, ry, rw, rh = entry
    # YOLOv5 box decoding: cx = (sigmoid(rx)*2 - 0.5 + c) * stride
    cx = (sigmoid_scalar(rx) * 2 - 0.5 + c) * 8
    cy = (sigmoid_scalar(ry) * 2 - 0.5 + r) * 8
    # Map model class index -> human label (with the swap applied)
    cls_idx = 0 if cls0 >= cls1 else 1
    cls_name = LABELS_MODEL_ORDER[cls_idx]
    print("  score={:.3f}  cell=({:>2d},{:>2d})  anchor={}  cls0={:.3f} cls1={:.3f}  -> {}  decoded_center=({:.0f},{:.0f})".format(
        score, r, c, a, cls0, cls1, cls_name, cx, cy))

if best_loc:
    r, c, a, cls0, cls1, rx, ry, rw, rh = best_loc
    print("\nAbsolute best (no threshold):")
    print("  cell=({},{})  anchor={}  score={:.3f}".format(r, c, a, best_score))
    print("  raw box: x={:.3f} y={:.3f} w={:.3f} h={:.3f}".format(rx, ry, rw, rh))
    print("  cls black={:.3f} silver={:.3f}".format(cls0, cls1))

print("\n--- done ---")
nn.shrink_memory_pool()

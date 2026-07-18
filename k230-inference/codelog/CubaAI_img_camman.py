# k230d_detect_anchorbasedet_final.py
# Static-image detection script for AI Cube AnchorBaseDet kmodel on K230D / K230D Zero.
#
# This version is built from the raw probe that matched PC simulator and K230D output.
#
# Key decisions:
#   1. Direct nn.kpu + nn.ai2d, not AIBase, so the pipeline is explicit.
#   2. AI Cube-style image loader: image.Image -> RGB888 -> HWC -> CHW.
#   3. No manual normalization. Your config says ptq_option "w:uint8 d:uint8".
#   4. Normal 3-output AnchorBaseDet postprocess:
#        output[0] = (1, 60, 80, 21), stride 8
#        output[1] = (1, 30, 40, 21), stride 16
#        output[2] = (1, 15, 20, 21), stride 32
#   5. Low confidence threshold by default because raw score probe showed useful scores around 0.05–0.35.
#   6. Optional RAW_TENSOR mode lets you feed the exact PC-created .bin tensor directly to KPU.

import gc
import time
import image
import aicube
import nncase_runtime as nn
import ulab.numpy as np


# ============================================================
# EDIT THESE PATHS / SETTINGS
# ============================================================
KMODEL_PATH = "/data/kmodel/best_AnchorBaseDet_can3_5_n_20260514232500.kmodel"

# Normal static image mode
IMAGE_PATH = "/data/dataset/1s0b/1s0b_0010.jpg"

# Optional exact raw tensor test mode.
# Use this only when you want to feed /data/pc_input_1x3x480x640_uint8.bin directly.
USE_RAW_TENSOR_INPUT = False
RAW_INPUT_PATH = "/data/pc_input_1x3x480x640_uint8.bin"

RESULT_PATH = "/data/det_result.jpg"
SAVE_RESULT = False

MODEL_W = 640
MODEL_H = 480

LABELS = ["black", "silver"]

STRIDES = [8, 16, 32]

ANCHORS = [
    33, 43,  41, 56,  55, 63,          # stride 8
    59, 81,  72, 92,  87, 111,         # stride 16
    103, 134, 161, 121, 134, 157,      # stride 32
]

# Debug threshold. After boxes are stable, you can test 0.1, 0.2, 0.3.
# Do not jump to 0.5 yet because your raw probe showed many useful candidates below 0.5.
CONFIDENCE_THRESHOLD = 0.05
NMS_THRESHOLD = 0.5
NMS_OPTION = False

# Set True if you want more raw output diagnostics.
PRINT_RAW_OUTPUT_INFO = True


# ============================================================
# Utility
# ============================================================
class ScopedTiming:
    def __init__(self, info="", enable=True):
        self.info = info
        self.enable = enable

    def __enter__(self):
        if self.enable:
            self.start = time.time_ns()
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        if self.enable:
            elapsed = (time.time_ns() - self.start) / 1000000
            print("{} took {:.2f} ms".format(self.info, elapsed))


def product_shape(shape):
    n = 1
    for s in shape:
        n *= s
    return n


def flatten_output(arr):
    n = product_shape(arr.shape)
    return arr.reshape((n,))


def safe_stats(arr):
    flat = flatten_output(arr)
    n = product_shape(arr.shape)

    if n <= 0:
        return 0.0, 0.0, 0.0

    mn = float(flat[0])
    mx = float(flat[0])
    total = 0.0

    for i in range(n):
        v = float(flat[i])
        if v < mn:
            mn = v
        if v > mx:
            mx = v
        total += v

    return mn, mx, total / n


def print_tensor_brief(name, arr):
    mn, mx, mean = safe_stats(arr)
    print("{} shape={} dtype={} min={:.6f} max={:.6f} mean={:.6f}".format(
        name, arr.shape, arr.dtype, mn, mx, mean
    ))

    flat = flatten_output(arr)
    n = product_shape(arr.shape)
    c = 20
    if c > n:
        c = n

    print("{} first {} values:".format(name, c))
    for i in range(c):
        print("  [{}] {}".format(i, flat[i]))


def read_img_chw_rgb888(path):
    """
    AI Cube-style static image loader:
      image.Image(path)
      -> to_rgb888()
      -> numpy HWC
      -> CHW

    Returns:
      img_chw: shape (3, H, W), uint8
      img_draw: RGB565 image object for drawing
    """
    img_obj = image.Image(path)
    img_draw = img_obj.to_rgb565()

    img_rgb888 = img_obj.to_rgb888()
    img_hwc = img_rgb888.to_numpy_ref()

    h = img_hwc.shape[0]
    w = img_hwc.shape[1]
    c = img_hwc.shape[2]

    img_tmp = img_hwc.reshape((h * w, c))
    img_tmp_t = img_tmp.transpose()
    img_chw = img_tmp_t.copy().reshape((c, h, w))

    return img_chw, img_draw


def calc_letterbox_pad(src_w, src_h, dst_w, dst_h):
    """
    Same padding calculation style as AI Cube generated det_image.py.
    Returns:
      top, bottom, left, right
    """
    ratiow = float(dst_w) / src_w
    ratioh = float(dst_h) / src_h

    if ratiow < ratioh:
        ratio = ratiow
    else:
        ratio = ratioh

    new_w = int(ratio * src_w)
    new_h = int(ratio * src_h)

    dw = float(dst_w - new_w) / 2
    dh = float(dst_h - new_h) / 2

    top = int(round(dh - 0.1))
    bottom = int(round(dh + 0.1))
    left = int(round(dw - 0.1))
    right = int(round(dw + 0.1))

    return top, bottom, left, right


def load_raw_tensor(path):
    """
    Load raw PC-created tensor:
      shape = (1, 3, 480, 640)
      dtype = uint8
    """
    expected = 1 * 3 * MODEL_H * MODEL_W

    with open(path, "rb") as f:
        raw = f.read()

    print("RAW tensor expected bytes:", expected)
    print("RAW tensor actual bytes  :", len(raw))

    if len(raw) != expected:
        raise ValueError("RAW input byte size mismatch")

    arr = np.frombuffer(raw, dtype=np.uint8)
    arr = arr.reshape((1, 3, MODEL_H, MODEL_W))
    return arr


def build_ai2d_and_run(img_chw):
    """
    Takes CHW image, applies AI2D letterbox/resize to [1,3,MODEL_H,MODEL_W].
    For 640x480 input and 640x480 model, pad should be all zero.
    """
    src_c = img_chw.shape[0]
    src_h = img_chw.shape[1]
    src_w = img_chw.shape[2]

    print("img_chw.shape:", img_chw.shape)
    print("source size [w,h]:", [src_w, src_h])

    top, bottom, left, right = calc_letterbox_pad(src_w, src_h, MODEL_W, MODEL_H)
    print("pad top,bottom,left,right:", top, bottom, left, right)

    ai2d_input = img_chw.reshape((1, src_c, src_h, src_w))
    ai2d_input_tensor = nn.from_numpy(ai2d_input)

    ai2d_out_np = np.ones((1, 3, MODEL_H, MODEL_W), dtype=np.uint8)
    ai2d_out_tensor = nn.from_numpy(ai2d_out_np)

    ai2d = nn.ai2d()
    ai2d.set_dtype(
        nn.ai2d_format.NCHW_FMT,
        nn.ai2d_format.NCHW_FMT,
        np.uint8,
        np.uint8
    )
    ai2d.set_pad_param(True, [0, 0, 0, 0, top, bottom, left, right], 0, [114, 114, 114])
    ai2d.set_resize_param(True, nn.interp_method.tf_bilinear, nn.interp_mode.half_pixel)

    ai2d_builder = ai2d.build(
        [1, 3, src_h, src_w],
        [1, 3, MODEL_H, MODEL_W]
    )

    with ScopedTiming("ai2d run", True):
        ai2d_builder.run(ai2d_input_tensor, ai2d_out_tensor)

    if PRINT_RAW_OUTPUT_INFO:
        try:
            arr = ai2d_out_tensor.to_numpy()
            print_tensor_brief("ai2d_out", arr)
        except Exception as e:
            print("Could not inspect ai2d_out:", e)

    # Cleanup AI2D objects after tensor is built.
    del ai2d_input_tensor
    del ai2d_builder
    del ai2d
    gc.collect()

    return ai2d_out_tensor, [src_w, src_h]


def get_kpu_outputs(kpu):
    try:
        n = kpu.outputs_size()
    except Exception as e:
        print("outputs_size() failed:", e)
        n = 3

    print("Number of output tensors:", n)

    outputs = []
    for i in range(n):
        t = kpu.get_output_tensor(i)
        arr = t.to_numpy()

        if PRINT_RAW_OUTPUT_INFO:
            print_tensor_brief("output[{}]".format(i), arr)

        flat = flatten_output(arr)
        outputs.append(flat)

        del t
        del arr
        gc.collect()

    return outputs


def draw_detections(det_boxes, img_draw):
    if not det_boxes:
        print("No objects detected.")
        return

    print("Detections:", len(det_boxes))

    colors = [
        (255, 0, 0),
        (0, 255, 0),
        (0, 0, 255),
        (255, 255, 0),
        (255, 0, 255),
        (0, 255, 255),
    ]

    # det_box format from aicube usually:
    # [class_id, score, x1, y1, x2, y2]
    for b in sorted(det_boxes, key=lambda x: -x[1]):
        cls = int(b[0])
        score = float(b[1])
        x1 = int(b[2])
        y1 = int(b[3])
        x2 = int(b[4])
        y2 = int(b[5])

        w = x2 - x1
        h = y2 - y1

        if cls >= 0 and cls < len(LABELS):
            label = LABELS[cls]
        else:
            label = "c{}".format(cls)

        color = colors[cls % len(colors)]

        print("  {:>7s} {:.3f} box=({},{})-({},{}) center=({},{})".format(
            label, score, x1, y1, x2, y2, int((x1+x2)//2), int((y1+y2)//2)
        ))

        # Guard against invalid box size.
        if w <= 0 or h <= 0:
            print("    skipped invalid box size w={}, h={}".format(w, h))
            continue

        img_draw.draw_rectangle(x1, y1, w, h, color=color, thickness=2)
        img_draw.draw_string_advanced(
            x1,
            max(0, y1 - 32),
            24,
            "{} {:.2f}".format(label, score),
            color=color
        )


def main():
    print("=== K230D AnchorBaseDet final static detection ===")
    print("KMODEL_PATH:", KMODEL_PATH)
    print("IMAGE_PATH :", IMAGE_PATH)
    print("USE_RAW_TENSOR_INPUT:", USE_RAW_TENSOR_INPUT)
    print("CONFIDENCE_THRESHOLD:", CONFIDENCE_THRESHOLD)
    print("NMS_THRESHOLD:", NMS_THRESHOLD)

    if USE_RAW_TENSOR_INPUT:
        # Exact PC tensor mode.
        input_np = load_raw_tensor(RAW_INPUT_PATH)
        input_tensor = nn.from_numpy(input_np)

        # For drawing, still load the image if available.
        try:
            _, img_draw = read_img_chw_rgb888(IMAGE_PATH)
            original_size = [MODEL_W, MODEL_H]
        except Exception as e:
            print("Could not load draw image:", e)
            img_draw = None
            original_size = [MODEL_W, MODEL_H]

        if PRINT_RAW_OUTPUT_INFO:
            print_tensor_brief("raw input tensor", input_np)

    else:
        # Normal image mode.
        img_chw, img_draw = read_img_chw_rgb888(IMAGE_PATH)
        input_tensor, original_size = build_ai2d_and_run(img_chw)

    print("original size for postprocess [w,h]:", original_size)
    print("model size [w,h]:", [MODEL_W, MODEL_H])

    # KPU
    kpu = nn.kpu()
    kpu.load_kmodel(KMODEL_PATH)

    with ScopedTiming("kpu run", True):
        kpu.set_input_tensor(0, input_tensor)
        kpu.run()

    outputs = get_kpu_outputs(kpu)

    if len(outputs) < 3:
        print("ERROR: expected 3 outputs for this AnchorBaseDet kmodel, got", len(outputs))
        print("No postprocess performed.")
        return

    # Postprocess
    with ScopedTiming("aicube postprocess", True):
        det_boxes = aicube.anchorbasedet_post_process(
            outputs[0],
            outputs[1],
            outputs[2],
            [MODEL_W, MODEL_H],       # kmodel frame size [w,h]
            original_size,            # original image size [w,h]
            STRIDES,
            len(LABELS),
            CONFIDENCE_THRESHOLD,
            NMS_THRESHOLD,
            ANCHORS,
            NMS_OPTION
        )

    print("Postprocess returned:", det_boxes)

    if img_draw is not None:
        draw_detections(det_boxes, img_draw)
        img_draw.compress_for_ide()

        if SAVE_RESULT:
            img_draw.save(RESULT_PATH, quality=90)
            print("Saved:", RESULT_PATH)

    # Cleanup
    del input_tensor
    del kpu
    del outputs
    gc.collect()

    print("=== end ===")


if __name__ == "__main__":
    main()

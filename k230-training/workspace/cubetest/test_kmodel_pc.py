#!/usr/bin/env python3
"""
Test an AI Cube AnchorBaseDet kmodel on PC via the nncase simulator.

This mirrors what the K230 deployment does (preprocess + kmodel + postprocess)
so you can compare against AI Cube's PC test on the same image and see if
the K230 deployment matches.

Usage (inside your convert Docker):
    cd /workspace/cubetest      # or wherever you put the files
    python3 test_kmodel_pc.py
"""

import os
import sys
import json
import math

import cv2
import numpy as np

try:
    import nncase as nc
except ImportError:
    print("nncase not importable. Run this inside the convert Docker.")
    sys.exit(1)

# ----------------------------------------------------------------------------
# Config — adjust paths if your cubetest layout differs
# ----------------------------------------------------------------------------
KMODEL_PATH   = "./best_AnchorBaseDet_can3_5_n_20260514232500.kmodel"
CONFIG_PATH   = "./deploy_config.json"
IMAGE_PATH    = "./1s0b_0010.jpg"
RESULT_PATH   = "./pc_det_result.jpg"

CONF_THR       = 0.3        # low to see everything
NMS_IOU_THR    = 0.45
MODEL_W        = 640
MODEL_H        = 480
STRIDES        = [8, 16, 32]

# Toggle these to test the same hypotheses as K230 deployment
APPLY_VFLIP    = False
APPLY_HFLIP    = False
LABELS         = ["black", "silver"]    # ordering from deploy_config.json

# ----------------------------------------------------------------------------
# Helpers
# ----------------------------------------------------------------------------
def sigmoid(x):
    return 1.0 / (1.0 + np.exp(-x))


def load_config():
    try:
        with open(CONFIG_PATH) as f:
            cfg = json.load(f)
        print(f"Loaded {CONFIG_PATH}")
        return cfg
    except Exception as e:
        print(f"Couldn't read {CONFIG_PATH}: {e}")
        sys.exit(1)


def letterbox_preprocess(img_bgr, target_w, target_h, pad_color=(114, 114, 114)):
    """OpenCV resize + letterbox to model input size. Returns NCHW uint8 RGB."""
    h, w = img_bgr.shape[:2]
    ratio = min(target_w / w, target_h / h)
    new_w, new_h = int(round(ratio * w)), int(round(ratio * h))
    resized = cv2.resize(img_bgr, (new_w, new_h), interpolation=cv2.INTER_LINEAR)

    canvas = np.full((target_h, target_w, 3), pad_color, dtype=np.uint8)
    dh = (target_h - new_h) // 2
    dw = (target_w - new_w) // 2
    canvas[dh:dh + new_h, dw:dw + new_w] = resized

    # BGR -> RGB
    canvas_rgb = cv2.cvtColor(canvas, cv2.COLOR_BGR2RGB)
    # HWC -> CHW, batch
    nchw = canvas_rgb.transpose(2, 0, 1)[None]
    return nchw, ratio, dw, dh


def nms(boxes, scores, iou_thr):
    """Simple NMS. boxes: (N,4) xyxy. scores: (N,)."""
    if len(boxes) == 0:
        return []
    boxes = np.asarray(boxes, dtype=np.float32)
    scores = np.asarray(scores, dtype=np.float32)
    x1, y1, x2, y2 = boxes[:, 0], boxes[:, 1], boxes[:, 2], boxes[:, 3]
    areas = (x2 - x1) * (y2 - y1)
    order = scores.argsort()[::-1]
    keep = []
    while order.size > 0:
        i = order[0]
        keep.append(int(i))
        xx1 = np.maximum(x1[i], x1[order[1:]])
        yy1 = np.maximum(y1[i], y1[order[1:]])
        xx2 = np.minimum(x2[i], x2[order[1:]])
        yy2 = np.minimum(y2[i], y2[order[1:]])
        w = np.maximum(0.0, xx2 - xx1)
        h = np.maximum(0.0, yy2 - yy1)
        inter = w * h
        ovr = inter / (areas[i] + areas[order[1:]] - inter + 1e-9)
        inds = np.where(ovr <= iou_thr)[0]
        order = order[inds + 1]
    return keep


def decode_head(head_out, anchors_xy, stride, conf_thr):
    """
    Decode one YOLOv5-style head.
    head_out shape: (H, W, A*(5+nc))  (NHWC squeezed)
    anchors_xy: list of (aw, ah) tuples, len == A
    Returns list of (x1, y1, x2, y2, score, cls_id) in model-input pixel space.
    """
    H, W, _ = head_out.shape
    A = len(anchors_xy)
    out = head_out.reshape(H, W, A, -1)            # (H, W, A, 5+nc)
    nc = out.shape[-1] - 5

    obj = sigmoid(out[..., 4])      # obj IS pre-sigmoid logit
    cls = out[..., 5:]              # class scores ALREADY post-sigmoid
    score = obj[..., None] * cls    # (H, W, A, nc)
    score_max = score.max(axis=-1)
    cls_id    = score.argmax(axis=-1)

    mask = score_max > conf_thr
    if mask.sum() == 0:
        return []

    ry, rx, ra = np.where(mask)
    rxy = out[ry, rx, ra, :2]
    rwh = out[ry, rx, ra, 2:4]
    sc  = score_max[ry, rx, ra]
    ci  = cls_id[ry, rx, ra]

    bx = (rxy[:, 0] * 2 - 0.5 + rx) * stride
    by = (rxy[:, 1] * 2 - 0.5 + ry) * stride
    aw = np.array([anchors_xy[a][0] for a in ra])
    ah = np.array([anchors_xy[a][1] for a in ra])
    bw = (rwh[:, 0] * 2) ** 2 * aw
    bh = (rwh[:, 1] * 2) ** 2 * ah
    x1 = bx - bw / 2
    y1 = by - bh / 2
    x2 = bx + bw / 2
    y2 = by + bh / 2

    return list(zip(x1, y1, x2, y2, sc, ci))


def undo_letterbox(box, ratio, dw, dh):
    x1, y1, x2, y2 = box
    return (
        (x1 - dw) / ratio,
        (y1 - dh) / ratio,
        (x2 - dw) / ratio,
        (y2 - dh) / ratio,
    )


# ----------------------------------------------------------------------------
# Main
# ----------------------------------------------------------------------------
def main():
    cfg = load_config()
    # Anchors as 3 strides x 3 anchors x (w, h)
    anchor_lol = cfg["anchors"]
    anchors_per_stride = []
    for stride_anchors in anchor_lol:
        pairs = [(stride_anchors[i], stride_anchors[i + 1])
                 for i in range(0, len(stride_anchors), 2)]
        anchors_per_stride.append(pairs)
    print(f"Anchors per stride: {anchors_per_stride}")

    # ---- Load and (optionally) flip image ----
    img = cv2.imread(IMAGE_PATH)
    if img is None:
        print(f"Could not read {IMAGE_PATH}")
        sys.exit(1)
    print(f"Image: {IMAGE_PATH}  shape={img.shape}")

    if APPLY_VFLIP:
        img = img[::-1].copy()
        print("Applied VFLIP")
    if APPLY_HFLIP:
        img = img[:, ::-1].copy()
        print("Applied HFLIP")

    nchw, ratio, dw, dh = letterbox_preprocess(img, MODEL_W, MODEL_H)
    print(f"Preprocessed -> {nchw.shape}, letterbox ratio={ratio:.3f}, dw={dw}, dh={dh}")

    # ---- Run kmodel via simulator ----
    sim = nc.Simulator()
    sim.load_model(open(KMODEL_PATH, "rb").read())
    sim.set_input_tensor(0, nc.RuntimeTensor.from_numpy(nchw.astype(np.uint8)))
    sim.run()

    # nncase Simulator API varies: some builds use outputs_size as attr, others as method
    n_out = sim.outputs_size() if callable(sim.outputs_size) else sim.outputs_size
    print(f"\nKmodel outputs: {n_out}")
    head_outs = []
    for i in range(n_out):
        arr = sim.get_output_tensor(i).to_numpy()
        print(f"  output[{i}] shape={arr.shape}  min={arr.min():.3f}  max={arr.max():.3f}")
        head_outs.append(arr[0])   # drop batch

    # ---- Decode each head ----
    all_dets = []
    for head_out, stride, anchors in zip(head_outs, STRIDES, anchors_per_stride):
        # head_out is (H, W, C) after batch drop
        dets = decode_head(head_out, anchors, stride, CONF_THR)
        print(f"  stride {stride}: {len(dets)} candidates above conf {CONF_THR}")
        all_dets.extend(dets)

    if not all_dets:
        print("\nNO detections above threshold.")
        return

    # ---- NMS ----
    boxes = [(d[0], d[1], d[2], d[3]) for d in all_dets]
    scores = [d[4] for d in all_dets]
    keep = nms(boxes, scores, NMS_IOU_THR)
    final = [all_dets[i] for i in keep]
    print(f"\nAfter NMS: {len(final)} detections")

    # ---- Draw on UN-flipped original image ----
    orig = cv2.imread(IMAGE_PATH)
    H_orig, W_orig = orig.shape[:2]
    for d in sorted(final, key=lambda x: -x[4]):
        x1, y1, x2, y2, sc, ci = d
        # Undo letterbox to get coords in flipped-frame
        x1, y1, x2, y2 = undo_letterbox((x1, y1, x2, y2), ratio, dw, dh)
        # Undo flip
        if APPLY_VFLIP:
            y1, y2 = H_orig - y2, H_orig - y1
        if APPLY_HFLIP:
            x1, x2 = W_orig - x2, W_orig - x1
        x1, y1, x2, y2 = int(x1), int(y1), int(x2), int(y2)
        ci = int(ci)
        name = LABELS[ci] if ci < len(LABELS) else f"c{ci}"
        cx, cy = (x1 + x2) // 2, (y1 + y2) // 2
        print(f"  {name:>7s} {sc:.3f}  box=({x1},{y1})-({x2},{y2})  center=({cx},{cy})")
        cv2.rectangle(orig, (x1, y1), (x2, y2), (0, 255, 0), 2)
        cv2.putText(orig, f"{name} {sc:.2f}", (x1, max(0, y1 - 5)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 1)

    cv2.imwrite(RESULT_PATH, orig)
    print(f"\nSaved -> {RESULT_PATH}")


if __name__ == "__main__":
    main()

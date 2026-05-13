#!/usr/bin/env python3
"""
Test a .kmodel using the nncase CPU simulator and compare against the float ONNX.

The simulator produces (close to) bit-exact output to what the K230D KPU runs,
so this is the right place to measure quantization loss without flashing the board.

Usage:
    python test_kmodel.py <model.kmodel> <model.onnx> <images_dir>
        [--conf 0.05] [--limit 20] [--img-height 480] [--img-width 640]

Output: a side-by-side table of float vs kmodel confidence per image and per class.
"""

import argparse
import os
import sys
from glob import glob

import cv2
import numpy as np
import onnxruntime as ort

# nncase runtime — exact import path varies slightly by version
try:
    import nncase_runtime as nncase
except ImportError:
    try:
        import nncase
    except ImportError:
        print("ERROR: install nncase runtime:  pip install nncase==2.9.0")
        sys.exit(1)


CONF_DEFAULT = 0.05
IOU_THRESH = 0.45


# ---------------------------------------------------------------------------
# YOLO postprocess (matches testing_onnx.py)
# ---------------------------------------------------------------------------
def postprocess(output, conf_thresh, img_height, img_width):
    raw = np.squeeze(output)
    if raw.ndim != 2:
        raise ValueError(f"Unexpected output shape: {raw.shape}")
    pred = raw.T if raw.shape[0] < raw.shape[1] else raw

    dets = []
    for row in pred:
        xc, yc, w, h = row[:4]
        scores = row[4:]
        conf = float(scores.max())
        if conf < conf_thresh:
            continue
        cls_id = int(scores.argmax())
        x1 = max(0, int(xc - w / 2))
        y1 = max(0, int(yc - h / 2))
        x2 = min(img_width, int(xc + w / 2))
        y2 = min(img_height, int(yc + h / 2))
        dets.append((x1, y1, x2, y2, conf, cls_id))
    return nms(dets, IOU_THRESH)


def nms(detections, iou_threshold):
    if not detections:
        return []
    detections = sorted(detections, key=lambda x: x[4], reverse=True)
    keep = []
    while detections:
        best = detections.pop(0)
        keep.append(best)
        bx1, by1, bx2, by2, _, bc = best
        ba = (bx2 - bx1) * (by2 - by1)

        def iou(o):
            xi1, yi1 = max(bx1, o[0]), max(by1, o[1])
            xi2, yi2 = min(bx2, o[2]), min(by2, o[3])
            inter = max(0, xi2 - xi1) * max(0, yi2 - yi1)
            area = (o[2] - o[0]) * (o[3] - o[1])
            return inter / (ba + area - inter) if (ba + area - inter) > 0 else 0

        detections = [d for d in detections if d[5] != bc or iou(d) < iou_threshold]
    return keep


# ---------------------------------------------------------------------------
# Inference backends
# ---------------------------------------------------------------------------
class FloatONNX:
    """Run the float ONNX model (same preprocess as testing_onnx.py)."""

    def __init__(self, onnx_path):
        self.sess = ort.InferenceSession(onnx_path, providers=["CPUExecutionProvider"])
        self.input_name = self.sess.get_inputs()[0].name

    def infer(self, bgr_img, h, w):
        img = cv2.resize(bgr_img, (w, h))
        img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
        img = img.transpose(2, 0, 1)[None].astype(np.float32) / 255.0
        return self.sess.run(None, {self.input_name: img})[0]


class KModelSim:
    """Run the kmodel via the nncase CPU simulator.

    Matches convert_to_kmodel3.py preprocess: input_type=uint8, NCHW, [0,255], RGB.
    The simulator handles the mean/std division internally because preprocess=True
    was set at compile time.
    """

    def __init__(self, kmodel_path):
        with open(kmodel_path, "rb") as f:
            data = f.read()
        # nncase 2.x API
        self.sim = nncase.Simulator()
        self.sim.load_model(data)

    def infer(self, bgr_img, h, w):
        img = cv2.resize(bgr_img, (w, h))
        img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
        img = img.transpose(2, 0, 1)[None].astype(np.uint8)
        tensor = nncase.RuntimeTensor.from_numpy(img)
        self.sim.set_input_tensor(0, tensor)
        self.sim.run()
        return self.sim.get_output_tensor(0).to_numpy()


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------
def best_detection_per_class(dets, num_classes=2):
    """Return list[(cls_id, conf)] with the highest-conf detection per class."""
    best = {}
    for *_, conf, cls in dets:
        if cls not in best or conf > best[cls]:
            best[cls] = conf
        if cls >= num_classes:
            num_classes = cls + 1
    return [(c, best.get(c, 0.0)) for c in range(num_classes)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("kmodel", help="Path to .kmodel")
    ap.add_argument("onnx", help="Path to float .onnx")
    ap.add_argument("images", help="Directory of test images")
    ap.add_argument("--conf", type=float, default=CONF_DEFAULT)
    ap.add_argument("--limit", type=int, default=20, help="Max images to test")
    ap.add_argument("--img-height", type=int, default=480)
    ap.add_argument("--img-width", type=int, default=640)
    ap.add_argument("--labels", nargs="+", default=["black", "silver"])
    args = ap.parse_args()

    h, w = args.img_height, args.img_width

    print(f"Loading float model:  {args.onnx}")
    onnx_model = FloatONNX(args.onnx)
    print(f"Loading kmodel:       {args.kmodel}")
    kmodel = KModelSim(args.kmodel)

    files = []
    for ext in ("*.jpg", "*.jpeg", "*.png", "*.bmp"):
        files.extend(glob(os.path.join(args.images, "**", ext), recursive=True))
    files = sorted(files)[: args.limit]
    if not files:
        print(f"No images in {args.images}")
        sys.exit(1)

    print(f"\nTesting {len(files)} images @ {w}x{h}, conf>={args.conf}")
    print(f"{'image':<32} {'class':<8} {'float':>7} {'kmodel':>7} {'delta':>7}")
    print("-" * 70)

    deltas = []
    for fp in files:
        img = cv2.imread(fp)
        if img is None:
            continue

        out_f = onnx_model.infer(img, h, w)
        out_k = kmodel.infer(img, h, w)

        det_f = postprocess(out_f, args.conf, h, w)
        det_k = postprocess(out_k, args.conf, h, w)

        ncls = len(args.labels)
        best_f = best_detection_per_class(det_f, ncls)
        best_k = best_detection_per_class(det_k, ncls)

        name = os.path.basename(fp)[:30]
        for (cls_f, cf), (_, ck) in zip(best_f, best_k):
            label = args.labels[cls_f] if cls_f < len(args.labels) else f"c{cls_f}"
            delta = ck - cf
            deltas.append(delta)
            marker = "  ⚠" if delta < -0.2 else ""
            print(f"{name:<32} {label:<8} {cf:>7.3f} {ck:>7.3f} {delta:>+7.3f}{marker}")
        print()

    if deltas:
        deltas = np.array(deltas)
        print("=" * 70)
        print(f"Δ summary over {len(deltas)} (image,class) pairs:")
        print(f"  mean   : {deltas.mean():+.3f}")
        print(f"  median : {np.median(deltas):+.3f}")
        print(f"  worst  : {deltas.min():+.3f}")
        print(f"  best   : {deltas.max():+.3f}")
        bad = (deltas < -0.2).sum()
        print(f"  >0.2 loss on {bad}/{len(deltas)} cases")
        print()
        if deltas.mean() > -0.05:
            print("✓ Quantization looks healthy.")
        elif deltas.mean() > -0.15:
            print("~ Moderate quant loss — try --w-quant-type int8, --calib-method NoClip.")
        else:
            print("✗ Heavy quant loss — calibration data is likely off-distribution.")


if __name__ == "__main__":
    main()

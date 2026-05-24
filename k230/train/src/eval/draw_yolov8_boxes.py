"""Run YOLOv8 kmodel on one image, draw decoded boxes, save annotated jpg.

Same letterbox + to_input_tensor path as pc_eval_yolov8.py and
debug_yolov8_one.py, so what you see is what calibration saw.
"""
from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

import cv2
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
from common.letterbox import letterbox_bgr, to_input_tensor  # noqa: E402


CATS = ["silver", "black"]
COLORS = [(0, 215, 255), (0, 0, 255)]  # silver=yellow-ish, black=red


def decode(out, num_classes, score_thr):
    o = out[0]
    bb = o[:4]
    cs = o[4:4 + num_classes]
    cls_id = cs.argmax(axis=0)
    cls_max = cs.max(axis=0)
    keep = cls_max >= score_thr
    if not keep.any():
        return []
    cx, cy, w, h = bb[0, keep], bb[1, keep], bb[2, keep], bb[3, keep]
    return list(zip(
        cls_id[keep].tolist(), cls_max[keep].tolist(),
        (cx - w / 2).tolist(), (cy - h / 2).tolist(),
        (cx + w / 2).tolist(), (cy + h / 2).tolist(),
    ))


def iou(a, b):
    ix1, iy1 = max(a[0], b[0]), max(a[1], b[1])
    ix2, iy2 = min(a[2], b[2]), min(a[3], b[3])
    iw, ih = max(0.0, ix2 - ix1), max(0.0, iy2 - iy1)
    inter = iw * ih
    if inter <= 0:
        return 0.0
    aa = (a[2] - a[0]) * (a[3] - a[1])
    bb = (b[2] - b[0]) * (b[3] - b[1])
    return inter / (aa + bb - inter + 1e-9)


def nms(boxes, iou_thr=0.5):
    out = []
    for c in {b[0] for b in boxes}:
        cb = sorted([b for b in boxes if b[0] == c], key=lambda b: -b[1])
        while cb:
            h = cb.pop(0)
            out.append(h)
            cb = [b for b in cb if iou(h[2:6], b[2:6]) < iou_thr]
    return sorted(out, key=lambda b: -b[1])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kmodel", required=True)
    ap.add_argument("--image", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--model-w", type=int, default=640)
    ap.add_argument("--model-h", type=int, default=480)
    ap.add_argument("--nc", type=int, default=2)
    ap.add_argument("--conf", type=float, default=0.05)
    ap.add_argument("--nms-iou", type=float, default=0.5)
    ap.add_argument("--max-draw", type=int, default=20)
    ap.add_argument("--channel-order", default="rgb",
                    choices=["rgb", "bgr", "gray3"])
    ap.add_argument("--apply-histeq", action="store_true",
                    help="CLAHE on Y channel (matches device path).")
    args = ap.parse_args()

    import nncase as nc
    sim = nc.Simulator()
    with open(args.kmodel, "rb") as f:
        sim.load_model(f.read())

    bgr = cv2.imread(args.image, cv2.IMREAD_COLOR)
    if bgr is None:
        raise SystemExit(f"cv2 failed to read {args.image}")

    if args.apply_histeq:
        ycrcb = cv2.cvtColor(bgr, cv2.COLOR_BGR2YCrCb)
        clahe = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8, 8))
        ycrcb[:, :, 0] = clahe.apply(ycrcb[:, :, 0])
        bgr = cv2.cvtColor(ycrcb, cv2.COLOR_YCrCb2BGR)

    letter, ratio, pad_lt = letterbox_bgr(bgr, args.model_w, args.model_h)
    inp = to_input_tensor(letter, channel_order=args.channel_order)

    sim.set_input_tensor(0, nc.RuntimeTensor.from_numpy(inp))
    sim.run()
    out = sim.get_output_tensor(0).to_numpy()

    decoded = decode(out, args.nc, args.conf)
    kept = nms(decoded, args.nms_iou)[:args.max_draw]

    canvas = letter.copy()
    for cls, score, x1, y1, x2, y2 in kept:
        color = COLORS[cls % len(COLORS)]
        cv2.rectangle(canvas, (int(x1), int(y1)), (int(x2), int(y2)),
                       color, 2)
        label = f"{CATS[cls % len(CATS)]} {score:.2f}"
        (tw, th), _ = cv2.getTextSize(label, cv2.FONT_HERSHEY_SIMPLEX, 0.5, 1)
        y_top = max(0, int(y1) - 4)
        cv2.rectangle(canvas, (int(x1), y_top - th - 4),
                       (int(x1) + tw + 4, y_top), color, -1)
        cv2.putText(canvas, label, (int(x1) + 2, y_top - 2),
                     cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1,
                     cv2.LINE_AA)

    header = (f"{Path(args.kmodel).parent.name}  "
              f"out:{out.shape} min={out.min():.2g} max={out.max():.2g}  "
              f"kept={len(kept)} conf>={args.conf}")
    cv2.rectangle(canvas, (0, 0), (canvas.shape[1], 22), (0, 0, 0), -1)
    cv2.putText(canvas, header, (4, 16), cv2.FONT_HERSHEY_SIMPLEX, 0.45,
                 (255, 255, 255), 1, cv2.LINE_AA)

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    cv2.imwrite(args.out, canvas)
    print(f"[OK] {args.kmodel}")
    print(f"  out shape={out.shape} dtype={out.dtype} "
          f"min={out.min():.4g} max={out.max():.4g}")
    print(f"  decoded(pre-nms)={len(decoded)}  kept={len(kept)}")
    for i, (cls, score, x1, y1, x2, y2) in enumerate(kept[:5]):
        print(f"    [{i}] {CATS[cls]} score={score:.4f} "
              f"xyxy=({x1:.0f},{y1:.0f},{x2:.0f},{y2:.0f})")
    print(f"  saved -> {args.out}")


if __name__ == "__main__":
    main()

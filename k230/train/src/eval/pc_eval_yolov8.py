"""PC evaluation for YOLOv8 .kmodel via nncase Simulator.

Differences from `pc_eval.py` (AnchorBaseDet):
- Output is a single tensor (1, 4+nc, N) where N = sum of grid_w*grid_h
  across strides 8/16/32. For 640x480: N = 80*60 + 40*30 + 20*15 = 6300.
- Each column carries (cx, cy, w, h, *cls_probs) already decoded into
  PIXEL coordinates of the MODEL input (0..640, 0..480) and class
  probabilities through sigmoid. We don't need anchors or strides.
- No `aicube.anchorbasedet_post_process`. Just argmax over class probs,
  score threshold, class-wise NMS.

Output:
  reports/eval_yolov8_<variant>.csv         per-GT rows
  reports/eval_yolov8_<variant>_summary.json
"""
from __future__ import annotations

import argparse
import csv
import glob
import json
import os
import random
import sys
from pathlib import Path

import numpy as np

try:
    import cv2
except ImportError as e:
    raise ImportError("opencv-python is required") from e


sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
from common.letterbox import (  # noqa: E402
    letterbox_bgr, to_input_tensor, transform_box_to_letter,
)
from common.decode import iou_xyxy  # noqa: E402


THRESHOLDS = [0.003, 0.005, 0.01, 0.03, 0.05, 0.10, 0.20, 0.40]


def read_yolo_label(label_path: str, ori_w: int, ori_h: int):
    gts = []
    if not os.path.exists(label_path):
        return gts
    with open(label_path) as f:
        for line in f:
            p = line.strip().split()
            if len(p) < 5:
                continue
            cls = int(float(p[0]))
            xc = float(p[1]) * ori_w
            yc = float(p[2]) * ori_h
            bw = float(p[3]) * ori_w
            bh = float(p[4]) * ori_h
            gts.append({"cls": cls,
                        "x1": xc - bw / 2.0, "y1": yc - bh / 2.0,
                        "x2": xc + bw / 2.0, "y2": yc + bh / 2.0})
    return gts


def find_images(images_dir: str, max_count: int, seed: int):
    exts = ("*.jpg", "*.jpeg", "*.png", "*.bmp")
    files = []
    for e in exts:
        files.extend(sorted(glob.glob(os.path.join(images_dir, e))))
        files.extend(sorted(glob.glob(os.path.join(images_dir, e.upper()))))
    if max_count > 0 and len(files) > max_count:
        rng = random.Random(seed)
        files = rng.sample(files, max_count)
    return sorted(files)


def decode_yolov8(output: np.ndarray, num_classes: int, score_threshold: float):
    """output shape (1, 4+nc, N). Return list of dicts with x1,y1,x2,y2,cls,score."""
    out = output[0]                                      # (4+nc, N)
    bboxes = out[:4]                                     # (4, N) -- cx, cy, w, h
    cls_scores = out[4:4 + num_classes]                  # (nc, N)

    cls_id = cls_scores.argmax(axis=0)                   # (N,)
    cls_max = cls_scores.max(axis=0)                     # (N,)
    keep = cls_max >= score_threshold
    if not keep.any():
        return []

    cx = bboxes[0, keep]
    cy = bboxes[1, keep]
    w  = bboxes[2, keep]
    h  = bboxes[3, keep]
    scores = cls_max[keep]
    classes = cls_id[keep]

    x1 = cx - w / 2.0
    y1 = cy - h / 2.0
    x2 = cx + w / 2.0
    y2 = cy + h / 2.0
    return list(zip(classes.tolist(), scores.tolist(),
                    x1.tolist(), y1.tolist(), x2.tolist(), y2.tolist()))


def nms_class_wise(boxes, iou_threshold: float = 0.5):
    """boxes: list of (cls, score, x1, y1, x2, y2)"""
    kept = []
    for cls in {b[0] for b in boxes}:
        cls_boxes = sorted([b for b in boxes if b[0] == cls],
                           key=lambda b: -b[1])
        while cls_boxes:
            head = cls_boxes.pop(0)
            kept.append(head)
            cls_boxes = [b for b in cls_boxes
                         if iou_xyxy(head[2:6], b[2:6]) < iou_threshold]
    return sorted(kept, key=lambda b: -b[1])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kmodel", required=True)
    ap.add_argument("--images-dir", required=True)
    ap.add_argument("--labels-dir", required=True)
    ap.add_argument("--model-w", type=int, default=640)
    ap.add_argument("--model-h", type=int, default=480)
    ap.add_argument("--num-classes", type=int, default=2)
    # YOLOv8 ONNX from Ultralytics was probably trained with this order:
    ap.add_argument("--categories", nargs="+",
                    default=["silver", "black"])
    ap.add_argument("--conf-thr", type=float, default=0.001,
                    help="Pre-NMS score threshold (very low for sweep)")
    ap.add_argument("--nms-thr", type=float, default=0.5)
    ap.add_argument("--limit", type=int, default=50)
    ap.add_argument("--channel-order", default="rgb",
                    choices=["rgb", "bgr", "gray3"])
    ap.add_argument("--apply-histeq", action="store_true",
                    help="Apply OpenCV CLAHE to mimic K230 img.histeq().")
    ap.add_argument("--out-csv", required=True)
    ap.add_argument("--eval-summary", required=True)
    ap.add_argument("--seed", type=int, default=42)
    args = ap.parse_args()

    import nncase as nc
    sim = nc.Simulator()
    with open(args.kmodel, "rb") as f:
        sim.load_model(f.read())

    files = find_images(args.images_dir, args.limit, args.seed)
    print(f"Evaluating {len(files)} images against {args.kmodel}")
    print(f"  model {args.model_w}x{args.model_h} | classes={args.categories} "
          f"| conf>={args.conf_thr} | nms={args.nms_thr}")

    if args.apply_histeq:
        clahe = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8, 8))

    rows = []
    total_gt = 0
    hit_by_th = {th: 0 for th in THRESHOLDS}

    for idx, fp in enumerate(files):
        bgr = cv2.imread(fp, cv2.IMREAD_COLOR)
        if bgr is None:
            continue
        ori_h, ori_w = bgr.shape[:2]
        gts_orig = read_yolo_label(os.path.join(args.labels_dir,
                                                 Path(fp).stem + ".txt"),
                                    ori_w, ori_h)
        if not gts_orig:
            continue

        # Optional histeq (per channel) to match training preprocessing
        if args.apply_histeq:
            ycrcb = cv2.cvtColor(bgr, cv2.COLOR_BGR2YCrCb)
            ycrcb[:, :, 0] = clahe.apply(ycrcb[:, :, 0])
            bgr = cv2.cvtColor(ycrcb, cv2.COLOR_YCrCb2BGR)

        letter, ratio, pad_lt = letterbox_bgr(bgr, args.model_w, args.model_h)
        gts_letter = []
        for g in gts_orig:
            x1, y1, x2, y2 = transform_box_to_letter(
                [g["x1"], g["y1"], g["x2"], g["y2"]], ratio, pad_lt,
            )
            gts_letter.append({**g, "lx1": x1, "ly1": y1, "lx2": x2, "ly2": y2})

        inp = to_input_tensor(letter, channel_order=args.channel_order)
        sim.set_input_tensor(0, nc.RuntimeTensor.from_numpy(inp))
        sim.run()
        out = sim.get_output_tensor(0).to_numpy()

        if idx == 0:
            print(f"  out[0] shape: {out.shape} dtype={out.dtype} "
                  f"min={out.min():.4g} max={out.max():.4g} "
                  f"mean={out.mean():.4g}")

        decoded = decode_yolov8(out, args.num_classes, args.conf_thr)
        boxes = nms_class_wise(decoded, args.nms_thr)

        for gt_i, gt in enumerate(gts_letter):
            total_gt += 1
            gt_box = [gt["lx1"], gt["ly1"], gt["lx2"], gt["ly2"]]
            same_cls = [b for b in boxes if b[0] == gt["cls"]]
            if not same_cls:
                rows.append({
                    "image": Path(fp).name, "gt_index": gt_i,
                    "gt_cls": args.categories[gt["cls"]],
                    "best_iou": 0.0, "best_iou_score": 0.0,
                    "best_score": 0.0, "best_score_iou": 0.0,
                })
                continue
            best_iou_box = max(same_cls, key=lambda b: iou_xyxy(b[2:6], gt_box))
            best_score_box = max(same_cls, key=lambda b: b[1])
            best_iou = iou_xyxy(best_iou_box[2:6], gt_box)
            best_score_iou = iou_xyxy(best_score_box[2:6], gt_box)

            for th in THRESHOLDS:
                if any((b[1] >= th and iou_xyxy(b[2:6], gt_box) >= 0.5)
                       for b in same_cls):
                    hit_by_th[th] += 1

            rows.append({
                "image": Path(fp).name, "gt_index": gt_i,
                "gt_cls": args.categories[gt["cls"]],
                "best_iou": best_iou,
                "best_iou_score": best_iou_box[1],
                "best_score": best_score_box[1],
                "best_score_iou": best_score_iou,
            })

        if (idx + 1) % 10 == 0:
            print(f"  [{idx + 1}/{len(files)}]  rows={len(rows)}")

    os.makedirs(os.path.dirname(args.out_csv) or ".", exist_ok=True)
    with open(args.out_csv, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)
    print(f"\nSaved {args.out_csv}")

    avg_best_iou = sum(r["best_iou"] for r in rows) / max(1, len(rows))
    avg_best_iou_score = sum(r["best_iou_score"] for r in rows) / max(1, len(rows))
    avg_best_score_iou = sum(r["best_score_iou"] for r in rows) / max(1, len(rows))
    recalls = {f"recall@IoU0.5_score>={th}":
               (hit_by_th[th] / total_gt if total_gt else 0.0)
               for th in THRESHOLDS}

    summary = {
        "backend": "nncase Simulator",
        "kmodel": args.kmodel,
        "model_type": "YOLOv8 anchor-free",
        "images_evaluated": len(files),
        "num_gt": total_gt,
        "avg_best_iou": avg_best_iou,
        "avg_best_iou_score": avg_best_iou_score,
        "avg_best_score_iou": avg_best_score_iou,
        "categories": args.categories,
        "conf_threshold": args.conf_thr,
        "nms_threshold": args.nms_thr,
        "apply_histeq": args.apply_histeq,
        **recalls,
    }
    with open(args.eval_summary, "w") as f:
        json.dump(summary, f, indent=2)
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()

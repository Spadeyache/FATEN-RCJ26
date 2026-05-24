"""Float32 evaluation of the reconstructed ONNX (no quantization).

Runs onnxruntime on the reconstructed `anchorbasedet_reconstructed.onnx`,
applies the same letterbox + ImageNet normalization that the kmodel would
internally apply via its baked preprocess, and scores against the same
GT labels as `pc_eval.py`.

This isolates the question: does the MODEL itself work in float32, or is
our reconstruction subtly wrong?

  - If recall is high here -> the model is fine; quantization is the bug.
  - If recall is also low -> reconstruction / decoder bug.

No nncase dependency. Runs on the Windows host.
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
from common.config import load_deploy_config  # noqa: E402
from common.letterbox import (  # noqa: E402
    letterbox_bgr, transform_box_to_letter,
)
from common.decode import decode_outputs, iou_xyxy  # noqa: E402


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
            gts.append({
                "cls": cls,
                "x1": xc - bw / 2.0,
                "y1": yc - bh / 2.0,
                "x2": xc + bw / 2.0,
                "y2": yc + bh / 2.0,
            })
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--onnx", default="data/anchorbasedet_reconstructed.onnx")
    ap.add_argument("--config", default="exports/v00_aicube_baseline/deploy_config.json",
                    help="Used for anchors/categories/img_size only (not kmodel_path)")
    ap.add_argument("--images-dir", required=True)
    ap.add_argument("--labels-dir", required=True)
    ap.add_argument("--limit", type=int, default=50)
    ap.add_argument("--out-csv", default="reports/eval_float.csv")
    ap.add_argument("--eval-summary", default="reports/eval_float_summary.json")
    ap.add_argument("--seed", type=int, default=42)
    args = ap.parse_args()

    import onnxruntime as ort
    sess = ort.InferenceSession(args.onnx,
                                 providers=["CPUExecutionProvider"])

    cfg = load_deploy_config(args.config)
    mean = np.array(cfg.mean, dtype=np.float32).reshape(1, 3, 1, 1)
    std = np.array(cfg.std, dtype=np.float32).reshape(1, 3, 1, 1)

    files = find_images(args.images_dir, args.limit, args.seed)
    print(f"Evaluating {len(files)} images in float32 via ONNX Runtime")

    rows = []
    total_gt = 0
    hit_by_th = {th: 0 for th in THRESHOLDS}

    for idx, fp in enumerate(files):
        bgr = cv2.imread(fp, cv2.IMREAD_COLOR)
        if bgr is None:
            continue
        ori_h, ori_w = bgr.shape[:2]
        label_path = os.path.join(args.labels_dir, Path(fp).stem + ".txt")
        gts_orig = read_yolo_label(label_path, ori_w, ori_h)
        if not gts_orig:
            continue

        letter, ratio, pad_lt = letterbox_bgr(bgr, cfg.width, cfg.height)
        gts_letter = []
        for g in gts_orig:
            x1, y1, x2, y2 = transform_box_to_letter(
                [g["x1"], g["y1"], g["x2"], g["y2"]], ratio, pad_lt,
            )
            gts_letter.append({**g, "lx1": x1, "ly1": y1, "lx2": x2, "ly2": y2})

        # Float32 RGB NCHW with ImageNet normalization
        rgb = cv2.cvtColor(letter, cv2.COLOR_BGR2RGB)
        chw = rgb.transpose(2, 0, 1).astype(np.float32) / 255.0
        x = chw[None]                                # (1, 3, H, W)
        x = (x - mean) / std

        outs = sess.run(None, {"input": x.astype(np.float32)})

        boxes = decode_outputs(outs, cfg.anchors_per_scale,
                               cfg.categories, cfg.num_classes)

        for gt_i, gt in enumerate(gts_letter):
            total_gt += 1
            gt_box = [gt["lx1"], gt["ly1"], gt["lx2"], gt["ly2"]]
            same_cls = [b for b in boxes if b.cls == gt["cls"]]
            if not same_cls:
                rows.append({
                    "image": Path(fp).name, "gt_index": gt_i,
                    "gt_cls": cfg.categories[gt["cls"]],
                    "best_iou": 0.0, "best_iou_score": 0.0,
                    "best_score": 0.0, "best_score_iou": 0.0,
                })
                continue
            best_iou_box = max(same_cls, key=lambda b: iou_xyxy(b.box, gt_box))
            best_score_box = max(same_cls, key=lambda b: b.score)
            best_iou = iou_xyxy(best_iou_box.box, gt_box)
            best_score_iou = iou_xyxy(best_score_box.box, gt_box)

            for th in THRESHOLDS:
                if any((b.score >= th and iou_xyxy(b.box, gt_box) >= 0.5)
                       for b in same_cls):
                    hit_by_th[th] += 1

            rows.append({
                "image": Path(fp).name,
                "gt_index": gt_i,
                "gt_cls": cfg.categories[gt["cls"]],
                "best_iou": best_iou,
                "best_iou_score": best_iou_box.score,
                "best_score": best_score_box.score,
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

    if rows:
        avg_best_iou = sum(r["best_iou"] for r in rows) / len(rows)
        avg_best_iou_score = sum(r["best_iou_score"] for r in rows) / len(rows)
        avg_best_score_iou = sum(r["best_score_iou"] for r in rows) / len(rows)
    else:
        avg_best_iou = avg_best_iou_score = avg_best_score_iou = 0.0

    recalls = {f"recall@IoU0.5_score>={th}":
               (hit_by_th[th] / total_gt if total_gt else 0.0)
               for th in THRESHOLDS}

    summary = {
        "backend": "onnxruntime",
        "onnx": args.onnx,
        "images_evaluated": len(files),
        "num_gt": total_gt,
        "avg_best_iou": avg_best_iou,
        "avg_best_iou_score": avg_best_iou_score,
        "avg_best_score_iou": avg_best_score_iou,
        **recalls,
    }
    with open(args.eval_summary, "w") as f:
        json.dump(summary, f, indent=2)
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()

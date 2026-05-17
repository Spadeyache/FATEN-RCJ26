"""Evaluate one .kmodel against YOLO-format GT labels via nncase Simulator.

Reads a variant's `deploy_config.json` to pick up anchors/categories/img_size
without hardcoding. Outputs:

  eval.csv          per-GT row with best_iou, best_score, best_iou_score, ...
  eval_summary.json aggregate recall@T and avg metrics
  debug/            optional annotated images (first N)

Designed to run in the same Linux Docker env that compiled the kmodel.

Letterbox preprocessing matches the K230D ai2d behavior:
  ratio = min(W/w, H/h); pad 114 to fill remaining area.
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
    letterbox_bgr, to_input_tensor, transform_box_to_letter,
    inverse_transform_box,
)
from common.decode import decode_outputs, iou_xyxy, nms_class_wise  # noqa: E402


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


def find_label_path(image_path: str, labels_dir: str) -> str:
    stem = Path(image_path).stem
    return os.path.join(labels_dir, stem + ".txt")


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


def draw_debug(img_bgr, gts, boxes_decoded, out_path, labels):
    img = img_bgr.copy()
    for gt in gts:
        cv2.rectangle(img, (int(gt["x1"]), int(gt["y1"])),
                      (int(gt["x2"]), int(gt["y2"])), (0, 255, 0), 2)
        name = labels[gt["cls"]] if gt["cls"] < len(labels) else f"c{gt['cls']}"
        cv2.putText(img, f"GT {name}",
                    (int(gt["x1"]), max(20, int(gt["y1"]) - 5)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 255, 0), 2)
    top = sorted(boxes_decoded, key=lambda b: -b.score)[:10]
    for b in top:
        color = (0, 0, 255) if b.cls == 1 else (255, 0, 0)
        cv2.rectangle(img, (int(b.x1), int(b.y1)),
                      (int(b.x2), int(b.y2)), color, 1)
        cv2.putText(img, f"{b.label} {b.score:.2f}",
                    (int(b.x1), max(20, int(b.y1) - 5)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.45, color, 1)
    cv2.imwrite(out_path, img)


def evaluate(kmodel_path: str, cfg, images_dir: str, labels_dir: str,
              limit: int, channel_order: str,
              out_csv: str, eval_summary: str,
              debug_dir: str, debug_count: int, seed: int) -> dict:
    import nncase as nc

    sim = nc.Simulator()
    with open(kmodel_path, "rb") as f:
        sim.load_model(f.read())

    files = find_images(images_dir, limit, seed)
    if not files:
        raise FileNotFoundError(f"No images in {images_dir}")
    print(f"Evaluating {len(files)} images against {kmodel_path}")

    if debug_dir:
        os.makedirs(debug_dir, exist_ok=True)

    rows = []
    total_gt = 0
    hit_by_th = {th: 0 for th in THRESHOLDS}

    for idx, fp in enumerate(files):
        bgr = cv2.imread(fp, cv2.IMREAD_COLOR)
        if bgr is None:
            print(f"  WARN: cv2.imread failed: {fp}")
            continue
        ori_h, ori_w = bgr.shape[:2]
        label_path = find_label_path(fp, labels_dir)
        gts_orig = read_yolo_label(label_path, ori_w, ori_h)
        if not gts_orig:
            continue

        letter, ratio, pad_lt = letterbox_bgr(bgr, cfg.width, cfg.height)
        # Map GT boxes into letterboxed coords for matching with decoded.
        gts_letter = []
        for g in gts_orig:
            x1, y1, x2, y2 = transform_box_to_letter(
                [g["x1"], g["y1"], g["x2"], g["y2"]], ratio, pad_lt,
            )
            gts_letter.append({**g, "lx1": x1, "ly1": y1, "lx2": x2, "ly2": y2})

        inp = to_input_tensor(letter, channel_order=channel_order)
        sim.set_input_tensor(0, nc.RuntimeTensor.from_numpy(inp))
        sim.run()
        outs = [sim.get_output_tensor(i).to_numpy() for i in range(3)]

        boxes = decode_outputs(
            outs, cfg.anchors_per_scale,
            cfg.categories, cfg.num_classes,
        )

        if debug_dir and idx < debug_count:
            stem = Path(fp).stem
            # Translate boxes back to ORIGINAL coords for the debug overlay.
            from common.decode import DecodedBox  # local import
            boxes_orig = []
            for b in boxes:
                x1, y1, x2, y2 = inverse_transform_box(
                    [b.x1, b.y1, b.x2, b.y2], ratio, pad_lt, ori_w, ori_h,
                )
                boxes_orig.append(DecodedBox(
                    cls=b.cls, label=b.label, score=b.score,
                    obj=b.obj, cls_score=b.cls_score,
                    x1=x1, y1=y1, x2=x2, y2=y2,
                    scale=b.scale, gy=b.gy, gx=b.gx, anchor=b.anchor,
                ))
            draw_debug(bgr, gts_orig, boxes_orig,
                       os.path.join(debug_dir, stem + "_debug.jpg"),
                       cfg.categories)

        for gt_i, gt in enumerate(gts_letter):
            total_gt += 1
            gt_box = [gt["lx1"], gt["ly1"], gt["lx2"], gt["ly2"]]
            same_cls = [b for b in boxes if b.cls == gt["cls"]]
            if not same_cls:
                rows.append({
                    "image": Path(fp).name, "gt_index": gt_i,
                    "gt_cls": cfg.categories[gt["cls"]] if gt["cls"] < len(cfg.categories) else f"c{gt['cls']}",
                    "best_iou": 0.0, "best_iou_score": 0.0,
                    "best_iou_obj": 0.0, "best_iou_cls_score": 0.0,
                    "best_score": 0.0, "best_score_iou": 0.0,
                })
                continue

            best_iou_box = max(same_cls, key=lambda b: iou_xyxy(b.box, gt_box))
            best_score_box = max(same_cls, key=lambda b: b.score)
            best_iou = iou_xyxy(best_iou_box.box, gt_box)
            best_score = best_score_box.score
            best_score_iou = iou_xyxy(best_score_box.box, gt_box)

            for th in THRESHOLDS:
                if any((b.score >= th and iou_xyxy(b.box, gt_box) >= 0.5)
                       for b in same_cls):
                    hit_by_th[th] += 1

            rows.append({
                "image": Path(fp).name,
                "gt_index": gt_i,
                "gt_cls": cfg.categories[gt["cls"]] if gt["cls"] < len(cfg.categories) else f"c{gt['cls']}",
                "best_iou": best_iou,
                "best_iou_score": best_iou_box.score,
                "best_iou_obj": best_iou_box.obj,
                "best_iou_cls_score": best_iou_box.cls_score,
                "best_score": best_score,
                "best_score_iou": best_score_iou,
            })

        if (idx + 1) % 10 == 0:
            print(f"  [{idx + 1}/{len(files)}]  rows={len(rows)}")

    # Write CSV
    os.makedirs(os.path.dirname(out_csv) or ".", exist_ok=True)
    with open(out_csv, "w", newline="") as f:
        fields = ["image", "gt_index", "gt_cls", "best_iou", "best_iou_score",
                  "best_iou_obj", "best_iou_cls_score",
                  "best_score", "best_score_iou"]
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    print(f"\nSaved {out_csv}")

    # Aggregate
    if rows:
        avg_best_iou = sum(r["best_iou"] for r in rows) / len(rows)
        avg_best_iou_score = sum(r["best_iou_score"] for r in rows) / len(rows)
        avg_best_score_iou = sum(r["best_score_iou"] for r in rows) / len(rows)
    else:
        avg_best_iou = avg_best_iou_score = avg_best_score_iou = 0.0

    recalls = {}
    for th in THRESHOLDS:
        recalls[f"recall@IoU0.5_score>={th}"] = (
            hit_by_th[th] / total_gt if total_gt else 0.0
        )

    summary = {
        "kmodel_path": kmodel_path,
        "images_evaluated": len(files),
        "num_gt": total_gt,
        "avg_best_iou": avg_best_iou,
        "avg_best_iou_score": avg_best_iou_score,
        "avg_best_score_iou": avg_best_score_iou,
        **recalls,
    }
    with open(eval_summary, "w") as f:
        json.dump(summary, f, indent=2)
    print(f"Saved {eval_summary}")
    print(json.dumps(summary, indent=2))
    return summary


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--config", required=True,
                    help="Path to variant deploy_config.json")
    ap.add_argument("--kmodel", help="Override .kmodel path")
    ap.add_argument("--images-dir", required=True)
    ap.add_argument("--labels-dir", required=True)
    ap.add_argument("--limit", type=int, default=50)
    ap.add_argument("--channel-order", default="rgb",
                    choices=["rgb", "bgr", "gray3"])
    ap.add_argument("--out-csv", default="eval.csv")
    ap.add_argument("--eval-summary", default="eval_summary.json")
    ap.add_argument("--debug-dir", default="")
    ap.add_argument("--debug-count", type=int, default=10)
    ap.add_argument("--seed", type=int, default=42)
    args = ap.parse_args()

    cfg = load_deploy_config(args.config)
    kmodel_path = args.kmodel or cfg.kmodel_path
    evaluate(kmodel_path, cfg, args.images_dir, args.labels_dir,
             args.limit, args.channel_order,
             args.out_csv, args.eval_summary,
             args.debug_dir, args.debug_count, args.seed)


if __name__ == "__main__":
    main()

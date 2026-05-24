"""Comprehensive YOLOv8 K230 quantization diagnostic.

For each kmodel × image:
  - raw max class score (pre-sigmoid not applicable: outputs already sigmoided)
  - top-20 decoded boxes BEFORE NMS
  - top boxes AFTER NMS
  - unique score values in top-20
  - count of boxes above 0.1 / 0.3 / 0.5
  - best-IoU box score (vs all GTs, IoU-maximizing match)
  - best-score box IoU (vs same-class GT)
  - annotated jpg

Outputs:
  reports/vis_debug/<model>/<image>.jpg   - annotated
  reports/diag_yolov8_quant.json          - all stats
  reports/diag_yolov8_quant_summary.csv   - flat table
"""
from __future__ import annotations

import argparse
import csv
import json
import os
import sys
from pathlib import Path

import cv2
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
from common.letterbox import (  # noqa: E402
    letterbox_bgr, to_input_tensor, transform_box_to_letter,
)


# IMPORTANT: dataset classes.txt order is [black, silver].
# Model class index order (from output channels 4,5) — established by
# inspecting float kmodel outputs: predicted "cls0" box overlaps the GT
# whose label cls=0 (which classes.txt names "black"). So model index 0
# and label index 0 align by POSITION. We label them by classes.txt.
LABEL_CATS = ["black", "silver"]
COLORS = [(0, 0, 255), (0, 215, 255)]  # black=red, silver=yellow-ish


def iou_xyxy(a, b):
    ix1, iy1 = max(a[0], b[0]), max(a[1], b[1])
    ix2, iy2 = min(a[2], b[2]), min(a[3], b[3])
    iw, ih = max(0.0, ix2 - ix1), max(0.0, iy2 - iy1)
    inter = iw * ih
    if inter <= 0:
        return 0.0
    aa = (a[2] - a[0]) * (a[3] - a[1])
    bb = (b[2] - b[0]) * (b[3] - b[1])
    return inter / (aa + bb - inter + 1e-9)


def decode_all(out, num_classes):
    """Return list of (cls_id, score, x1, y1, x2, y2) for all 6300 anchors."""
    o = out[0]
    bb = o[:4]
    cs = o[4:4 + num_classes]
    cls_id = cs.argmax(axis=0)
    cls_max = cs.max(axis=0)
    cx, cy, w, h = bb[0], bb[1], bb[2], bb[3]
    x1 = cx - w / 2
    y1 = cy - h / 2
    x2 = cx + w / 2
    y2 = cy + h / 2
    boxes = list(zip(cls_id.tolist(), cls_max.tolist(),
                     x1.tolist(), y1.tolist(),
                     x2.tolist(), y2.tolist()))
    return boxes


def nms(boxes, iou_thr=0.5):
    out = []
    for c in {b[0] for b in boxes}:
        cb = sorted([b for b in boxes if b[0] == c], key=lambda b: -b[1])
        while cb:
            h = cb.pop(0)
            out.append(h)
            cb = [b for b in cb if iou_xyxy(h[2:6], b[2:6]) < iou_thr]
    return sorted(out, key=lambda b: -b[1])


def read_yolo_label(label_path, ori_w, ori_h):
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
                        "x1": xc - bw / 2, "y1": yc - bh / 2,
                        "x2": xc + bw / 2, "y2": yc + bh / 2})
    return gts


def annotate(letter_bgr, kept_boxes, gts_letter, header):
    canvas = letter_bgr.copy()
    for g in gts_letter:
        cv2.rectangle(canvas, (int(g["lx1"]), int(g["ly1"])),
                      (int(g["lx2"]), int(g["ly2"])), (0, 255, 0), 1)
        cv2.putText(canvas, f"GT:{LABEL_CATS[g['cls']]}",
                    (int(g["lx1"]), int(g["ly1"]) - 2),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.4, (0, 255, 0), 1)
    for cls, score, x1, y1, x2, y2 in kept_boxes[:8]:
        color = COLORS[cls % len(COLORS)]
        cv2.rectangle(canvas, (int(x1), int(y1)), (int(x2), int(y2)),
                      color, 2)
        label = f"{LABEL_CATS[cls % len(LABEL_CATS)]} {score:.3f}"
        cv2.putText(canvas, label, (int(x1) + 2, int(y1) + 14),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.45, color, 1)
    cv2.rectangle(canvas, (0, 0), (canvas.shape[1], 22), (0, 0, 0), -1)
    cv2.putText(canvas, header, (4, 16), cv2.FONT_HERSHEY_SIMPLEX, 0.42,
                (255, 255, 255), 1, cv2.LINE_AA)
    return canvas


def run_one(sim, kmodel_name, fp, label_dir, model_w, model_h, num_classes,
            nms_iou, out_dir):
    import nncase as nc
    bgr = cv2.imread(fp, cv2.IMREAD_COLOR)
    if bgr is None:
        return None
    ori_h, ori_w = bgr.shape[:2]
    letter, ratio, pad_lt = letterbox_bgr(bgr, model_w, model_h)
    inp = to_input_tensor(letter, channel_order="rgb")

    sim.set_input_tensor(0, nc.RuntimeTensor.from_numpy(inp))
    sim.run()
    out = sim.get_output_tensor(0).to_numpy()

    boxes = decode_all(out, num_classes)
    sorted_by_score = sorted(boxes, key=lambda b: -b[1])
    top20 = sorted_by_score[:20]
    kept = nms([b for b in sorted_by_score if b[1] >= 0.05], nms_iou)

    raw_max = float(max(b[1] for b in boxes))
    unique_top20 = len({round(b[1], 4) for b in top20})
    n_above_010 = sum(1 for b in boxes if b[1] >= 0.10)
    n_above_030 = sum(1 for b in boxes if b[1] >= 0.30)
    n_above_050 = sum(1 for b in boxes if b[1] >= 0.50)

    label_fp = os.path.join(label_dir, Path(fp).stem + ".txt") if label_dir else ""
    gts_orig = read_yolo_label(label_fp, ori_w, ori_h)
    gts_letter = []
    for g in gts_orig:
        x1, y1, x2, y2 = transform_box_to_letter(
            [g["x1"], g["y1"], g["x2"], g["y2"]], ratio, pad_lt)
        gts_letter.append({**g, "lx1": x1, "ly1": y1, "lx2": x2, "ly2": y2})

    per_gt = []
    for g in gts_letter:
        gt_box = [g["lx1"], g["ly1"], g["lx2"], g["ly2"]]
        # Same-class candidates only (cls index alignment by position)
        same_cls = [b for b in kept if b[0] == g["cls"]]
        if not same_cls:
            per_gt.append({"cls": g["cls"], "best_iou": 0.0,
                           "best_iou_score": 0.0, "best_score": 0.0,
                           "best_score_iou": 0.0})
            continue
        best_iou_box = max(same_cls, key=lambda b: iou_xyxy(b[2:6], gt_box))
        best_score_box = max(same_cls, key=lambda b: b[1])
        per_gt.append({
            "cls": g["cls"],
            "best_iou": iou_xyxy(best_iou_box[2:6], gt_box),
            "best_iou_score": best_iou_box[1],
            "best_score": best_score_box[1],
            "best_score_iou": iou_xyxy(best_score_box[2:6], gt_box),
        })

    # Annotate and save
    header = (f"{kmodel_name}  raw_max={raw_max:.3f}  "
              f"top20uniq={unique_top20}  "
              f">0.1:{n_above_010} >0.3:{n_above_030} >0.5:{n_above_050}")
    canvas = annotate(letter, kept, gts_letter, header)
    out_img = os.path.join(out_dir, kmodel_name, Path(fp).name)
    os.makedirs(os.path.dirname(out_img), exist_ok=True)
    cv2.imwrite(out_img, canvas)

    return {
        "model": kmodel_name,
        "image": Path(fp).name,
        "raw_max_score": raw_max,
        "out_shape": list(out.shape),
        "out_min": float(out.min()),
        "out_max": float(out.max()),
        "top20_pre_nms": [
            {"cls": int(b[0]), "score": b[1],
             "xyxy": [b[2], b[3], b[4], b[5]]} for b in top20
        ],
        "post_nms": [
            {"cls": int(b[0]), "score": b[1],
             "xyxy": [b[2], b[3], b[4], b[5]]} for b in kept[:10]
        ],
        "unique_top20_scores": sorted({round(b[1], 4) for b in top20},
                                       reverse=True),
        "n_unique_top20": unique_top20,
        "n_above_0.1": n_above_010,
        "n_above_0.3": n_above_030,
        "n_above_0.5": n_above_050,
        "n_gts": len(gts_letter),
        "per_gt": per_gt,
        "annotated_path": out_img,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kmodels", nargs="+", required=True)
    ap.add_argument("--images", nargs="+", required=True)
    ap.add_argument("--label-dir", default="")
    ap.add_argument("--out-dir", default="reports/vis_debug")
    ap.add_argument("--out-json", default="reports/diag_yolov8_quant.json")
    ap.add_argument("--out-csv", default="reports/diag_yolov8_quant_summary.csv")
    ap.add_argument("--model-w", type=int, default=640)
    ap.add_argument("--model-h", type=int, default=480)
    ap.add_argument("--nc", type=int, default=2)
    ap.add_argument("--nms-iou", type=float, default=0.5)
    args = ap.parse_args()

    import nncase as nc
    all_rows = []
    for km in args.kmodels:
        name = Path(km).parent.name
        print(f"\n=== {name} ===")
        sim = nc.Simulator()
        with open(km, "rb") as f:
            sim.load_model(f.read())
        for img in args.images:
            r = run_one(sim, name, img, args.label_dir, args.model_w,
                        args.model_h, args.nc, args.nms_iou, args.out_dir)
            if r is None:
                continue
            all_rows.append(r)
            print(f"  {Path(img).name}: raw_max={r['raw_max_score']:.4f} "
                  f"uniq{r['n_unique_top20']}/20 "
                  f">0.3={r['n_above_0.3']} "
                  f"gts={r['n_gts']} "
                  f"per_gt_best_iou={[round(g['best_iou'],2) for g in r['per_gt']]}")

    os.makedirs(os.path.dirname(args.out_json) or ".", exist_ok=True)
    with open(args.out_json, "w") as f:
        json.dump(all_rows, f, indent=2)

    # Flat CSV: model, image, raw_max, uniq, >0.3, >0.5,
    #          mean_best_iou, mean_best_score, mean_best_score_iou
    with open(args.out_csv, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["model", "image", "raw_max", "n_unique_top20",
                    "n_above_0.1", "n_above_0.3", "n_above_0.5",
                    "n_gts", "mean_best_iou", "mean_best_iou_score",
                    "mean_best_score", "mean_best_score_iou"])
        for r in all_rows:
            pg = r["per_gt"]
            mean = lambda k: (sum(g[k] for g in pg) / len(pg)) if pg else 0.0
            w.writerow([r["model"], r["image"], f"{r['raw_max_score']:.4f}",
                        r["n_unique_top20"], r["n_above_0.1"],
                        r["n_above_0.3"], r["n_above_0.5"],
                        r["n_gts"],
                        f"{mean('best_iou'):.4f}",
                        f"{mean('best_iou_score'):.4f}",
                        f"{mean('best_score'):.4f}",
                        f"{mean('best_score_iou'):.4f}"])
    print(f"\nWrote {args.out_json}")
    print(f"Wrote {args.out_csv}")


if __name__ == "__main__":
    main()

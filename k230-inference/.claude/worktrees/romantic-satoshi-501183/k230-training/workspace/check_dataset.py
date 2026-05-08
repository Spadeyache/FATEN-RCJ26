#!/usr/bin/env python3
"""
Dataset audit script - run this before retraining
Points to your YOLO dataset labels folder
"""

import os
from glob import glob
from collections import defaultdict

# ── CHANGE THIS to your dataset root ──────────────────────────
DATASET_ROOT = "/datasets/my_dataset"
LABELS       = ["black", "silver"]
# ──────────────────────────────────────────────────────────────

def audit_split(split_name, label_dir, image_dir):
    print(f"\n[{split_name.upper()}] ── {label_dir}")

    label_files = glob(os.path.join(label_dir, "*.txt"))
    image_files = glob(os.path.join(image_dir, "*.jpg")) + \
                  glob(os.path.join(image_dir, "*.png"))

    if not label_files:
        print(f"  ⚠ No label files found in {label_dir}")
        return

    print(f"  Images      : {len(image_files)}")
    print(f"  Label files : {len(label_files)}")

    class_counts    = defaultdict(int)
    empty_labels    = 0
    box_sizes       = []
    bad_files       = []

    for lf in label_files:
        with open(lf, "r") as f:
            lines = [l.strip() for l in f.readlines() if l.strip()]

        if not lines:
            empty_labels += 1
            continue

        for line in lines:
            parts = line.split()
            if len(parts) != 5:
                bad_files.append(lf)
                continue
            cls_id = int(parts[0])
            w, h   = float(parts[3]), float(parts[4])  # normalized box w/h
            class_counts[cls_id] += 1
            box_sizes.append((w, h))

    print(f"\n  Class breakdown:")
    total_boxes = sum(class_counts.values())
    for cls_id, name in enumerate(LABELS):
        count = class_counts.get(cls_id, 0)
        pct   = 100 * count / total_boxes if total_boxes else 0
        bar   = "█" * int(pct / 2)
        flag  = "  ⚠ UNDERREPRESENTED" if pct < 20 else ""
        print(f"    [{cls_id}] {name:10s}: {count:4d} boxes  ({pct:5.1f}%)  {bar}{flag}")

    print(f"\n  Empty label files (background) : {empty_labels}")
    print(f"  Malformed label lines          : {len(bad_files)}")

    if box_sizes:
        ws = [b[0] for b in box_sizes]
        hs = [b[1] for b in box_sizes]
        print(f"\n  Box size stats (normalized 0-1):")
        print(f"    width  min={min(ws):.3f}  max={max(ws):.3f}  mean={sum(ws)/len(ws):.3f}")
        print(f"    height min={min(hs):.3f}  max={max(hs):.3f}  mean={sum(hs)/len(hs):.3f}")
        tiny = sum(1 for w,h in box_sizes if w < 0.02 or h < 0.02)
        if tiny:
            print(f"    ⚠ {tiny} boxes are very small (w or h < 2% of image) — may hurt training")

    # Check for images without label files
    label_stems = {os.path.splitext(os.path.basename(f))[0] for f in label_files}
    image_stems = {os.path.splitext(os.path.basename(f))[0] for f in image_files}
    missing_labels = image_stems - label_stems
    if missing_labels:
        print(f"\n  ⚠ {len(missing_labels)} images have no label file:")
        for s in list(missing_labels)[:5]:
            print(f"    {s}")
        if len(missing_labels) > 5:
            print(f"    ... and {len(missing_labels)-5} more")


# Run audit on train and val splits
for split in ["train", "val"]:
    img_dir = os.path.join(DATASET_ROOT, split, "images")
    lbl_dir = os.path.join(DATASET_ROOT, split, "labels")
    if os.path.exists(lbl_dir):
        audit_split(split, lbl_dir, img_dir)
    else:
        print(f"\n[{split.upper()}] ⚠ Not found: {lbl_dir}")

print("\n[DONE] Paste this output and we can decide what to fix before retraining.")
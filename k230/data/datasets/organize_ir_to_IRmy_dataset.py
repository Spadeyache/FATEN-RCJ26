#!/usr/bin/env python3
"""
Split a flat CVAT / export folder (images/ + labels/) into YOLO layout matching
datasets/my_dataset: train/images, train/labels, val/images, val/labels.

Defaults:
  --src   datasets/IR-2026-05-07-18-52
  --out   datasets/IRmy_dataset

Optional copies classes.txt to the output root for reference.

Usage (from repo k230-training):
  py -3 datasets/organize_ir_to_IRmy_dataset.py
  py -3 datasets/organize_ir_to_IRmy_dataset.py --train-ratio 0.85 --seed 42
"""

from __future__ import annotations

import argparse
import random
import shutil
from pathlib import Path


IMAGE_SUFFIXES = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}


def paired_samples(src: Path) -> tuple[list[tuple[Path, Path]], list[str]]:
    warnings: list[str] = []
    img_dir = src / "images"
    lbl_dir = src / "labels"
    if not img_dir.is_dir():
        raise SystemExit(f"Missing images folder: {img_dir}")
    if not lbl_dir.is_dir():
        raise SystemExit(f"Missing labels folder: {lbl_dir}")

    pairs: list[tuple[Path, Path]] = []
    for img_path in sorted(img_dir.iterdir()):
        if not img_path.is_file() or img_path.suffix.lower() not in IMAGE_SUFFIXES:
            continue
        stem = img_path.stem
        lbl_path = lbl_dir / f"{stem}.txt"
        if not lbl_path.is_file():
            warnings.append(f"No label for image: {img_path.name}")
            continue
        pairs.append((img_path, lbl_path))

    for lbl_path in lbl_dir.glob("*.txt"):
        stem = lbl_path.stem
        if not any(p[0].stem == stem for p in pairs):
            if not any(img_dir.glob(stem + ".*")):
                warnings.append(f"Stale label file (no image): {lbl_path.name}")

    return pairs, warnings


def copy_pairs(pairs: list[tuple[Path, Path]], dest_img: Path, dest_lbl: Path) -> None:
    dest_img.mkdir(parents=True, exist_ok=True)
    dest_lbl.mkdir(parents=True, exist_ok=True)
    for img_path, lbl_path in pairs:
        shutil.copy2(img_path, dest_img / img_path.name)
        shutil.copy2(lbl_path, dest_lbl / lbl_path.name)


def main() -> None:
    here = Path(__file__).resolve().parent
    default_src = here / "IR-2026-05-07-18-52"
    default_out = here / "IRmy_dataset"

    parser = argparse.ArgumentParser(
        description="Organize flat IR annotated export into IRmy_dataset/ (YOLO splits like my_dataset)."
    )
    parser.add_argument("--src", type=Path, default=default_src, help="Flat dataset root with images/ and labels/")
    parser.add_argument("--out", type=Path, default=default_out, help="Output dataset root")
    parser.add_argument(
        "--train-ratio",
        type=float,
        default=0.85,
        help="Fraction of paired samples assigned to train (rest go to val).",
    )
    parser.add_argument("--seed", type=int, default=42, help="RNG seed for deterministic split.")
    parser.add_argument(
        "--force",
        action="store_true",
        help="Delete existing --out folder before writing.",
    )
    args = parser.parse_args()

    if not (0.0 < args.train_ratio < 1.0):
        raise SystemExit("--train-ratio must be between 0 and 1 (exclusive).")

    args.src = args.src.expanduser().resolve()
    args.out = args.out.expanduser().resolve()

    pairs, warnings = paired_samples(args.src)
    for w in warnings:
        print(f"WARNING: {w}")

    if not pairs:
        raise SystemExit("No image+label pairs found; aborting.")

    if args.force and args.out.exists():
        shutil.rmtree(args.out)

    rng = random.Random(args.seed)
    rng.shuffle(pairs)
    n = len(pairs)
    if n >= 2:
        n_train = int(round(n * args.train_ratio))
        n_train = max(1, min(n - 1, n_train))
        train_pairs = pairs[:n_train]
        val_pairs = pairs[n_train:]
    else:
        train_pairs = pairs[:]
        val_pairs = []
        print("WARNING: Only one annotated pair found; val split will be empty.")

    copy_pairs(train_pairs, args.out / "train" / "images", args.out / "train" / "labels")
    copy_pairs(val_pairs, args.out / "val" / "images", args.out / "val" / "labels")

    classes_src = args.src / "classes.txt"
    if classes_src.is_file():
        shutil.copy2(classes_src, args.out / "classes.txt")

    print(f"OK Source: {args.src}")
    print(f"OK Output: {args.out}")
    print(f"  train: {len(train_pairs)} pairs")
    print(f"  val:   {len(val_pairs)} pairs")
    if classes_src.is_file():
        print("  copied classes.txt to output root")


if __name__ == "__main__":
    main()

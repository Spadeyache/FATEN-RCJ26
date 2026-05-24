"""Compare PC nncase Simulator outputs to K230D KPU outputs on the same input.

Run after using src/probe/pc_raw_probe.py to save out0/out1/out2.npy on PC
and src/deploy/k230_raw_probe.py to save out0/out1/out2.npy on K230D for
the same input.bin.

Prints per-output L1, max-abs, cosine similarity. Detects whether PC and
K230 see "essentially the same" outputs (within quantization noise).
"""
from __future__ import annotations

import argparse
import os
import sys

import numpy as np


def cosine(a: np.ndarray, b: np.ndarray) -> float:
    a = a.flatten().astype(np.float64)
    b = b.flatten().astype(np.float64)
    na = np.linalg.norm(a)
    nb = np.linalg.norm(b)
    if na == 0 or nb == 0:
        return 0.0
    return float(np.dot(a, b) / (na * nb))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pc-dir", required=True,
                    help="Directory with pc_out0.npy/pc_out1.npy/pc_out2.npy")
    ap.add_argument("--k230-dir", required=True,
                    help="Directory with k230_out0.npy/k230_out1.npy/k230_out2.npy")
    ap.add_argument("--pc-prefix", default="pc_out")
    ap.add_argument("--k230-prefix", default="k230_out")
    args = ap.parse_args()

    print(f"PC dir:   {args.pc_dir}")
    print(f"K230 dir: {args.k230_dir}")

    rows = []
    for i in range(3):
        pc_path = os.path.join(args.pc_dir, f"{args.pc_prefix}{i}.npy")
        k_path = os.path.join(args.k230_dir, f"{args.k230_prefix}{i}.npy")
        if not (os.path.exists(pc_path) and os.path.exists(k_path)):
            print(f"  WARN: missing out[{i}] file(s)")
            continue
        pc = np.load(pc_path).astype(np.float32)
        kp = np.load(k_path).astype(np.float32)
        if pc.shape != kp.shape:
            print(f"  out[{i}] shape mismatch: pc={pc.shape} k230={kp.shape}")
            continue
        d = np.abs(pc - kp)
        row = {
            "i": i,
            "shape": list(pc.shape),
            "mean_abs": float(d.mean()),
            "max_abs": float(d.max()),
            "p95_abs": float(np.percentile(d, 95)),
            "cosine": cosine(pc, kp),
        }
        rows.append(row)
        print(f"  out[{i}] shape={row['shape']}  "
              f"mean|diff|={row['mean_abs']:.4g}  "
              f"max|diff|={row['max_abs']:.4g}  "
              f"p95|diff|={row['p95_abs']:.4g}  "
              f"cos={row['cosine']:.4f}")

    if rows:
        any_bad = any(r["mean_abs"] > 1e-3 for r in rows)
        all_cos = all(r["cosine"] > 0.99 for r in rows)
        print()
        if all_cos and not any_bad:
            print("PASS: PC and K230 outputs are essentially identical.")
        elif all_cos:
            print("WARN: cosine OK but mean|diff| > 1e-3; check quant/seed.")
        else:
            print("FAIL: outputs differ materially.")


if __name__ == "__main__":
    main()

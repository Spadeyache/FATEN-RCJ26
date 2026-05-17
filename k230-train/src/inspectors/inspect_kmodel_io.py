"""Inspect a .kmodel: input/output count, shapes, dtypes via nncase Simulator.

Runs a zero-tensor inference to expose the actual I/O contract that the
runtime sees. Use this on the baseline before believing the user's
documented (1,60,80,21) output shape.
"""
from __future__ import annotations

import argparse
import os
import sys

import numpy as np

try:
    import nncase as nc
except ImportError as e:
    print(f"ERROR: nncase import failed: {e}", file=sys.stderr)
    sys.exit(2)


def get_outputs_size(sim) -> int:
    """nncase 2.9 exposes outputs_size as either a method or a property."""
    if hasattr(sim, "outputs_size"):
        attr = sim.outputs_size
        try:
            return int(attr())
        except TypeError:
            return int(attr)
    # Fallback: probe up to 16 outputs.
    for i in range(16):
        try:
            sim.get_output_tensor(i)
        except Exception:
            return i
    return 16


def main():
    p = argparse.ArgumentParser()
    p.add_argument("kmodel_path")
    p.add_argument("--input-shape", default="1,3,480,640",
                   help="Comma-separated input shape for the zero tensor")
    p.add_argument("--input-dtype", default="uint8",
                   help="uint8 or float32")
    args = p.parse_args()

    if not os.path.exists(args.kmodel_path):
        print(f"ERROR: not found: {args.kmodel_path}", file=sys.stderr)
        sys.exit(2)

    shape = tuple(int(x) for x in args.input_shape.split(","))
    dtype = np.uint8 if args.input_dtype == "uint8" else np.float32

    print("=" * 60)
    print(f"  inspect_kmodel_io: {args.kmodel_path}")
    print("=" * 60)
    print(f"  size:   {os.path.getsize(args.kmodel_path):,} bytes")
    print(f"  probe:  shape={shape} dtype={dtype.__name__}")

    sim = nc.Simulator()
    with open(args.kmodel_path, "rb") as f:
        sim.load_model(f.read())

    zero = np.zeros(shape, dtype=dtype)
    try:
        sim.set_input_tensor(0, nc.RuntimeTensor.from_numpy(zero))
    except Exception as e:
        print(f"WARN: set_input_tensor with {args.input_dtype}/{shape} failed: {e}")
        print("Trying float32 fallback...")
        zero = np.zeros(shape, dtype=np.float32)
        sim.set_input_tensor(0, nc.RuntimeTensor.from_numpy(zero))

    try:
        sim.run()
    except Exception as e:
        print(f"ERROR: simulator.run() failed: {e}", file=sys.stderr)
        sys.exit(1)

    n_out = get_outputs_size(sim)
    print(f"\n  outputs: {n_out}")
    for i in range(n_out):
        out = sim.get_output_tensor(i).to_numpy()
        print(f"  out[{i}]: shape={out.shape} dtype={out.dtype} "
              f"min={out.min():.4g} max={out.max():.4g} mean={out.mean():.4g}")

    print()
    print("Layout interpretation:")
    print("  AnchorBaseDet expects (1, H, W, 21) NHWC at strides 8/16/32 ->")
    print("  shapes (1,60,80,21), (1,30,40,21), (1,15,20,21) for 640x480 input.")
    print("  If shapes are (1,21,60,80) etc, the kmodel uses NCHW output and")
    print("  the decoder must transpose.")


if __name__ == "__main__":
    main()

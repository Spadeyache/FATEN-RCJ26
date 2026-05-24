"""Lightweight wrapper around pc_raw_probe.py for the JPG -> Simulator path.

Useful when you just want to feed an image through a kmodel and inspect
output stats, without saving any tensors. (For the save-everything variant,
use `pc_raw_probe.py --image`.)
"""
from __future__ import annotations

import argparse
import os
import sys

import numpy as np


sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
from common.letterbox import letterbox_bgr, to_input_tensor  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kmodel", required=True)
    ap.add_argument("--image", required=True)
    ap.add_argument("--input-width", type=int, default=640)
    ap.add_argument("--input-height", type=int, default=480)
    ap.add_argument("--channel-order", default="rgb",
                    choices=["rgb", "bgr", "gray3"])
    args = ap.parse_args()

    import cv2
    import nncase as nc

    bgr = cv2.imread(args.image, cv2.IMREAD_COLOR)
    if bgr is None:
        print(f"ERROR: cv2.imread failed: {args.image}", file=sys.stderr)
        sys.exit(2)
    letter, ratio, pad_lt = letterbox_bgr(bgr, args.input_width,
                                          args.input_height)
    inp = to_input_tensor(letter, channel_order=args.channel_order)

    print(f"Input: {args.image} -> letter ratio={ratio:.3f} pad={pad_lt}")
    print(f"  tensor: shape={inp.shape} min={inp.min()} max={inp.max()} "
          f"mean={inp.mean():.2f}")

    sim = nc.Simulator()
    with open(args.kmodel, "rb") as f:
        sim.load_model(f.read())
    sim.set_input_tensor(0, nc.RuntimeTensor.from_numpy(inp))
    sim.run()
    for i in range(3):
        o = sim.get_output_tensor(i).to_numpy()
        print(f"  out[{i}]: shape={o.shape} min={o.min():.4g} "
              f"max={o.max():.4g} mean={o.mean():.4g}")


if __name__ == "__main__":
    main()

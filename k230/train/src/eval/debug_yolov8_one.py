"""Diagnose YOLOv8 kmodel output on one image. Print top boxes vs GT."""
from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

import numpy as np
import cv2


sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
from common.letterbox import letterbox_bgr, to_input_tensor, transform_box_to_letter  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kmodel", required=True)
    ap.add_argument("--image", required=True)
    ap.add_argument("--label", required=True)
    ap.add_argument("--model-w", type=int, default=640)
    ap.add_argument("--model-h", type=int, default=480)
    ap.add_argument("--nc", type=int, default=2)
    args = ap.parse_args()

    import nncase as nc
    sim = nc.Simulator()
    with open(args.kmodel, "rb") as f:
        sim.load_model(f.read())

    bgr = cv2.imread(args.image, cv2.IMREAD_COLOR)
    ori_h, ori_w = bgr.shape[:2]
    print(f"image: {args.image}  ori={ori_w}x{ori_h}")

    # GT
    with open(args.label) as f:
        for line in f:
            p = line.strip().split()
            if len(p) < 5: continue
            cls = int(float(p[0]))
            xc = float(p[1]) * ori_w; yc = float(p[2]) * ori_h
            bw = float(p[3]) * ori_w; bh = float(p[4]) * ori_h
            print(f"  GT cls={cls}  orig (x1,y1,x2,y2)=({xc-bw/2:.0f},{yc-bh/2:.0f},{xc+bw/2:.0f},{yc+bh/2:.0f})")

    letter, ratio, pad_lt = letterbox_bgr(bgr, args.model_w, args.model_h)
    print(f"  letterbox: ratio={ratio:.4f} pad={pad_lt}")

    # Re-print GT in letterboxed coords
    with open(args.label) as f:
        for line in f:
            p = line.strip().split()
            if len(p) < 5: continue
            cls = int(float(p[0]))
            xc = float(p[1]) * ori_w; yc = float(p[2]) * ori_h
            bw = float(p[3]) * ori_w; bh = float(p[4]) * ori_h
            x1, y1, x2, y2 = transform_box_to_letter(
                [xc-bw/2, yc-bh/2, xc+bw/2, yc+bh/2], ratio, pad_lt)
            print(f"  GT cls={cls}  letter (x1,y1,x2,y2)=({x1:.0f},{y1:.0f},{x2:.0f},{y2:.0f})  "
                  f"(cx,cy,w,h)=({(x1+x2)/2:.0f},{(y1+y2)/2:.0f},{x2-x1:.0f},{y2-y1:.0f})")

    inp = to_input_tensor(letter, channel_order="rgb")
    sim.set_input_tensor(0, nc.RuntimeTensor.from_numpy(inp))
    sim.run()
    out = sim.get_output_tensor(0).to_numpy()
    print(f"  out[0] shape={out.shape} dtype={out.dtype} min={out.min():.4g} max={out.max():.4g}")

    # Investigate first 4 channels (bbox) and last 2 (cls) separately
    bbox = out[0, :4]              # (4, 6300)
    cls = out[0, 4:4+args.nc]      # (nc, 6300)
    print(f"  bbox channel stats:")
    for i, name in enumerate(["ch0", "ch1", "ch2", "ch3"]):
        print(f"    {name}: min={bbox[i].min():.3f} max={bbox[i].max():.3f} mean={bbox[i].mean():.3f}")
    print(f"  cls channel stats:")
    for i in range(args.nc):
        print(f"    cls{i}: min={cls[i].min():.4f} max={cls[i].max():.4f} mean={cls[i].mean():.4f}")

    # Top 5 anchors by max class score
    cls_max = cls.max(axis=0)
    cls_id = cls.argmax(axis=0)
    top_idx = np.argsort(-cls_max)[:10]
    print(f"  top-10 anchors by max class score:")
    for j, i in enumerate(top_idx):
        b0, b1, b2, b3 = bbox[:, i]
        c0, c1 = cls[0, i], cls[1, i]
        # Try interpretations:
        # (a) cxcywh
        x1a, y1a, x2a, y2a = b0 - b2/2, b1 - b3/2, b0 + b2/2, b1 + b3/2
        # (b) xyxy
        x1b, y1b, x2b, y2b = b0, b1, b2, b3
        # (c) DFL ltrb at anchor i (need grid mapping); skip for now
        print(f"    [{j}] i={i} bbox=({b0:.1f},{b1:.1f},{b2:.1f},{b3:.1f}) "
              f"cls=({c0:.3f},{c1:.3f}) "
              f"cxcywh->xyxy=({x1a:.0f},{y1a:.0f},{x2a:.0f},{y2a:.0f}) "
              f"xyxy=({x1b:.0f},{y1b:.0f},{x2b:.0f},{y2b:.0f})")


if __name__ == "__main__":
    main()

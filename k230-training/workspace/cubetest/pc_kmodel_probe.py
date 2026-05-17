#!/usr/bin/env python3
"""
pc_kmodel_probe.py
PC nncase simulator raw-output probe for K230/K230D .kmodel.

Goal:
  Prove what the .kmodel outputs on PC simulator for the exact image tensor.
  This does NOT decode boxes. It only prints raw output shape/range and top grid scores.

Usage:
  python3 pc_kmodel_probe.py --kmodel /workspace/best.kmodel --image /datasets/my_dataset/val/images/img.jpg
  python3 pc_kmodel_probe.py --kmodel /workspace/best.kmodel --img-dir /datasets/my_dataset/val/images --index 0
"""

import argparse
import os
import sys
import numpy as np
import cv2
import nncase as nc


def get_outputs_size(sim):
    """Handle nncase versions where outputs_size is a method or property."""
    v = getattr(sim, "outputs_size", None)
    if callable(v):
        return v()
    if isinstance(v, int):
        return v
    # fallback: try outputs until failure
    n = 0
    while True:
        try:
            sim.get_output_tensor(n)
            n += 1
        except Exception:
            break
    return n


def pick_image(args):
    if args.image:
        return args.image
    if not args.img_dir:
        raise ValueError("Use --image or --img-dir")
    files = [f for f in sorted(os.listdir(args.img_dir))
             if f.lower().endswith((".jpg", ".jpeg", ".png", ".bmp"))]
    if not files:
        raise FileNotFoundError(f"No images found in {args.img_dir}")
    if args.index < 0 or args.index >= len(files):
        raise IndexError(f"--index {args.index} out of range; {len(files)} images found")
    return os.path.join(args.img_dir, files[args.index])


def make_input_tensor(image_path, width, height, save_npy=None):
    bgr = cv2.imread(image_path)
    if bgr is None:
        raise FileNotFoundError(f"cv2.imread failed: {image_path}")

    # Match your earlier PC command:
    # resize to 640x480, BGR->RGB, HWC->CHW, add batch, uint8.
    resized = cv2.resize(bgr, (width, height), interpolation=cv2.INTER_LINEAR)
    rgb = cv2.cvtColor(resized, cv2.COLOR_BGR2RGB)
    chw = rgb.transpose(2, 0, 1)[None].astype(np.uint8)

    if save_npy:
        np.save(save_npy, chw)
        print(f"Saved PC input tensor to: {save_npy}")

    return chw


def summarize_one_output(out, topk=10):
    print(f"    shape={out.shape} dtype={out.dtype} min={out.min():.6f} max={out.max():.6f} mean={out.mean():.6f}")
    flat = out.reshape(-1)
    print("    first 20 values:", np.array2string(flat[:20], precision=6, separator=', '))

    # Special diagnostic for your observed shape: (1, 60, 80, 21)
    if out.shape == (1, 60, 80, 21):
        o = out[0].reshape(60, 80, 3, 7)
        obj = o[..., 4]
        cls0 = o[..., 5]
        cls1 = o[..., 6]
        cls_max = np.maximum(cls0, cls1)
        score_raw = obj * cls_max

        print("    one-output YOLO-style probe:")
        print(f"      obj   min/max/mean = {obj.min():.6f} / {obj.max():.6f} / {obj.mean():.6f}")
        print(f"      cls0  min/max      = {cls0.min():.6f} / {cls0.max():.6f}")
        print(f"      cls1  min/max      = {cls1.min():.6f} / {cls1.max():.6f}")
        print(f"      score_raw max      = {score_raw.max():.6f}")
        print(f"      score_raw > 0.05   = {int((score_raw > 0.05).sum())}")
        print(f"      score_raw > 0.10   = {int((score_raw > 0.10).sum())}")
        print(f"      score_raw > 0.30   = {int((score_raw > 0.30).sum())}")

        idxs = np.argsort(score_raw.reshape(-1))[-topk:][::-1]
        print(f"      top {topk} raw-score cells:")
        for rank, idx in enumerate(idxs, start=1):
            h, w, a = np.unravel_index(idx, score_raw.shape)
            winner = "black" if cls0[h, w, a] >= cls1[h, w, a] else "silver"
            print(
                f"        #{rank:02d} cell=({h:02d},{w:02d}) anchor={a} "
                f"obj={obj[h,w,a]:.6f} cls0={cls0[h,w,a]:.6f} cls1={cls1[h,w,a]:.6f} "
                f"score={score_raw[h,w,a]:.6f} -> {winner}"
            )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kmodel", required=True, help="Path to .kmodel")
    ap.add_argument("--image", default=None, help="Single image path")
    ap.add_argument("--img-dir", default=None, help="Image directory")
    ap.add_argument("--index", type=int, default=0, help="Image index when using --img-dir")
    ap.add_argument("--width", type=int, default=640)
    ap.add_argument("--height", type=int, default=480)
    ap.add_argument("--save-input-npy", default=None, help="Optional path to save final input tensor .npy")
    ap.add_argument("--save-output-prefix", default=None, help="Optional prefix for output tensor .npy files")
    args = ap.parse_args()

    image_path = pick_image(args)

    print("=== PC nncase simulator raw-output probe ===")
    print("kmodel:", args.kmodel)
    print("image :", image_path)
    print("input : RGB, resize {}x{}, CHW, uint8, batch=1".format(args.width, args.height))

    inp = make_input_tensor(image_path, args.width, args.height, args.save_input_npy)
    print(f"input tensor shape={inp.shape} dtype={inp.dtype} min={inp.min()} max={inp.max()} mean={inp.mean():.3f}")
    print("input first 20 values:", inp.reshape(-1)[:20])

    sim = nc.Simulator()
    with open(args.kmodel, "rb") as f:
        sim.load_model(f.read())

    # Input descriptor is useful when available.
    try:
        desc = sim.get_input_desc(0)
        print("input desc:", desc)
    except Exception as e:
        print("input desc unavailable:", repr(e))

    sim.set_input_tensor(0, nc.RuntimeTensor.from_numpy(inp))
    sim.run()

    n = get_outputs_size(sim)
    print("Number of output tensors:", n)
    for i in range(n):
        out = sim.get_output_tensor(i).to_numpy()
        print(f"  output[{i}]")
        summarize_one_output(out)
        if args.save_output_prefix:
            path = f"{args.save_output_prefix}_out{i}.npy"
            np.save(path, out)
            print("    saved:", path)


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print("ERROR:", repr(e), file=sys.stderr)
        sys.exit(1)

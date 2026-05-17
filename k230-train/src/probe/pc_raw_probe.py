"""PC nncase Simulator probe: save input as .bin, save outputs as .npy.

Two modes:
  --image <jpg>           letterbox the image to model size and use as input
  --raw-bin <bin>         load a pre-saved uint8 NCHW input tensor

The exact input bin is also saved so K230D can load the SAME bytes.
"""
from __future__ import annotations

import argparse
import os
import sys

import numpy as np

try:
    import cv2
except ImportError:
    cv2 = None


sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
from common.letterbox import letterbox_bgr, to_input_tensor  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kmodel", required=True)
    ap.add_argument("--image", help="JPG/PNG to letterbox into the model")
    ap.add_argument("--raw-bin", help="Pre-saved uint8 NCHW bin")
    ap.add_argument("--input-width", type=int, default=640)
    ap.add_argument("--input-height", type=int, default=480)
    ap.add_argument("--channel-order", default="rgb",
                    choices=["rgb", "bgr", "gray3"])
    ap.add_argument("--save-input", default="probe_input.bin",
                    help="Save the resolved input tensor as a raw bin")
    ap.add_argument("--save-output-prefix", default="pc_out",
                    help="Prefix for output .npy files (pc_out0.npy, ...)")
    ap.add_argument("--out-dir", default="reports/probe_pc")
    args = ap.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)

    if args.raw_bin:
        if not os.path.exists(args.raw_bin):
            print(f"ERROR: not found: {args.raw_bin}", file=sys.stderr)
            sys.exit(2)
        raw = np.fromfile(args.raw_bin, dtype=np.uint8)
        expected = 1 * 3 * args.input_height * args.input_width
        if raw.size != expected:
            print(f"ERROR: raw bin size {raw.size} != expected {expected}",
                  file=sys.stderr)
            sys.exit(1)
        inp = raw.reshape(1, 3, args.input_height, args.input_width)
        print(f"Loaded raw bin: {args.raw_bin}")
    elif args.image:
        if cv2 is None:
            print("ERROR: cv2 needed for --image", file=sys.stderr)
            sys.exit(2)
        bgr = cv2.imread(args.image, cv2.IMREAD_COLOR)
        if bgr is None:
            print(f"ERROR: cv2.imread failed: {args.image}", file=sys.stderr)
            sys.exit(2)
        letter, ratio, pad_lt = letterbox_bgr(
            bgr, args.input_width, args.input_height,
        )
        inp = to_input_tensor(letter, channel_order=args.channel_order)
        print(f"Letterboxed {args.image}: ratio={ratio:.3f} pad={pad_lt}")
    else:
        print("ERROR: pass --image or --raw-bin", file=sys.stderr)
        sys.exit(2)

    # Save the exact tensor as a bin so K230D can load it.
    bin_path = os.path.join(args.out_dir, args.save_input)
    inp.astype(np.uint8).tofile(bin_path)
    print(f"Saved input bin -> {bin_path} ({inp.nbytes} bytes)")

    # Print input stats.
    print(f"\nInput tensor: shape={inp.shape} dtype={inp.dtype} "
          f"min={inp.min()} max={inp.max()} mean={inp.mean():.2f}")
    print(f"First 20 values: {inp.flatten()[:20].tolist()}")

    # Run the kmodel.
    import nncase as nc
    sim = nc.Simulator()
    with open(args.kmodel, "rb") as f:
        sim.load_model(f.read())
    sim.set_input_tensor(0, nc.RuntimeTensor.from_numpy(inp))
    sim.run()

    n = 3
    for i in range(n):
        try:
            o = sim.get_output_tensor(i).to_numpy()
        except Exception as e:
            print(f"out[{i}] not available: {e}")
            continue
        npy_path = os.path.join(args.out_dir,
                                 f"{args.save_output_prefix}{i}.npy")
        np.save(npy_path, o)
        print(f"out[{i}]: shape={o.shape} dtype={o.dtype} "
              f"min={o.min():.4g} max={o.max():.4g} mean={o.mean():.4g}  "
              f"-> {npy_path}")

        if o.ndim == 4 and o.shape[-1] == 21:
            # AnchorBaseDet: extract top objectness for sanity.
            H, W = o.shape[1], o.shape[2]
            y = o[0].reshape(H, W, 3, 7)
            obj = y[..., 4]
            cls_max = y[..., 5:7].max(axis=-1)
            score = obj * cls_max
            print(f"   top-3 obj*cls cells:")
            flat = score.reshape(-1)
            top = np.argpartition(-flat, 3)[:3]
            for idx in top[np.argsort(-flat[top])]:
                gy = (idx // 3) // W
                gx = (idx // 3) % W
                ai = idx % 3
                print(f"     gy={gy:3d} gx={gx:3d} a={ai} "
                      f"score={float(flat[idx]):.4f}")


if __name__ == "__main__":
    main()

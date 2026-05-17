# pc_make_raw_tensor_probe_v2.py
# PC nncase simulator probe using exact raw RGB CHW uint8 tensor.
#
# v2 fix:
#   Do NOT rely on sim.outputs_size().
#   Some nncase Python versions do not expose outputs_size(), so the old script
#   fell back to 1 output even when the model has 3 outputs.
#
# This script explicitly tries output indices 0,1,2 by default.

import argparse
import os
import cv2
import numpy as np
import nncase as nc


def summarize(name, arr):
    flat = arr.reshape(-1)
    print(f"{name} shape={arr.shape} dtype={arr.dtype} min={flat.min():.6f} max={flat.max():.6f} mean={flat.mean():.6f}")
    print(f"{name} first 20 values:", flat[:20])


def analyze_output(out, idx):
    summarize(f"output[{idx}]", out)

    if len(out.shape) == 4 and out.shape[3] == 21:
        h, w = out.shape[1], out.shape[2]
        y = out[0].reshape(h, w, 3, 7)
        obj = y[..., 4]
        cls0 = y[..., 5]
        cls1 = y[..., 6]
        score = obj * np.maximum(cls0, cls1)

        print(f"output[{idx}] YOLO-style probe:")
        print(f"  obj min/max/mean = {obj.min():.6f} / {obj.max():.6f} / {obj.mean():.6f}")
        print(f"  cls0 min/max     = {cls0.min():.6f} / {cls0.max():.6f}")
        print(f"  cls1 min/max     = {cls1.min():.6f} / {cls1.max():.6f}")
        print(f"  score_raw max    = {score.max():.6f}")
        print(f"  score_raw >0.05  = {(score > 0.05).sum()}")
        print(f"  score_raw >0.10  = {(score > 0.10).sum()}")
        print(f"  score_raw >0.30  = {(score > 0.30).sum()}")

        flat_idx = np.argsort(score.reshape(-1))[-10:][::-1]
        print("  top 10 raw-score cells:")
        for rank, fi in enumerate(flat_idx, 1):
            yy, xx, a = np.unravel_index(fi, score.shape)
            winner = "black" if cls0[yy, xx, a] >= cls1[yy, xx, a] else "silver"
            print(
                f"    #{rank:02d} cell=({yy},{xx}) anchor={a} "
                f"obj={obj[yy,xx,a]:.6f} cls0={cls0[yy,xx,a]:.6f} "
                f"cls1={cls1[yy,xx,a]:.6f} score={score[yy,xx,a]:.6f} -> {winner}"
            )


def load_or_make_input(args):
    if args.raw_bin:
        expected = 1 * 3 * args.height * args.width
        raw = np.fromfile(args.raw_bin, dtype=np.uint8)
        if raw.size != expected:
            raise ValueError(f"Raw bin has {raw.size} bytes, expected {expected}")
        inp = raw.reshape(1, 3, args.height, args.width)
        return inp

    img = cv2.imread(args.image, cv2.IMREAD_COLOR)
    if img is None:
        raise FileNotFoundError(args.image)

    img = cv2.resize(img, (args.width, args.height), interpolation=cv2.INTER_LINEAR)
    img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
    return img.transpose(2, 0, 1)[None].astype(np.uint8)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--kmodel", required=True)
    p.add_argument("--image")
    p.add_argument("--raw-bin", help="Use existing raw uint8 tensor instead of decoding image")
    p.add_argument("--width", type=int, default=640)
    p.add_argument("--height", type=int, default=480)
    p.add_argument("--out-bin")
    p.add_argument("--save-output-prefix")
    p.add_argument("--try-outputs", type=int, default=3, help="Try output indices 0..N-1")
    args = p.parse_args()

    if not args.image and not args.raw_bin:
        raise ValueError("Provide either --image or --raw-bin")

    print("=== PC raw tensor + simulator probe v2 ===")
    print("kmodel:", args.kmodel)
    print("image :", args.image)
    print("rawbin:", args.raw_bin)
    print("try outputs:", args.try_outputs)

    inp = load_or_make_input(args)
    summarize("input", inp)

    if args.out_bin:
        inp.tofile(args.out_bin)
        print("Saved raw uint8 tensor:", args.out_bin)
        print("Raw file size:", os.path.getsize(args.out_bin), "bytes")

    sim = nc.Simulator()
    sim.load_model(open(args.kmodel, "rb").read())
    sim.set_input_tensor(0, nc.RuntimeTensor.from_numpy(inp))
    sim.run()

    found = 0
    for i in range(args.try_outputs):
        try:
            out = sim.get_output_tensor(i).to_numpy()
        except Exception as e:
            print(f"output[{i}] read failed: {repr(e)}")
            continue

        found += 1
        analyze_output(out, i)

        if args.save_output_prefix:
            out_path = f"{args.save_output_prefix}_out{i}.npy"
            np.save(out_path, out)
            print("saved:", out_path)

    print("Successfully read outputs:", found)


if __name__ == "__main__":
    main()

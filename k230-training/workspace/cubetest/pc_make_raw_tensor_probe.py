# pc_make_raw_tensor_probe.py
# Create the exact RGB CHW uint8 input tensor used by PC nncase Simulator,
# save it as a raw .bin file, and run the kmodel on PC.
#
# Then copy the generated .bin file to K230D and run k230d_raw_tensor_probe.py.
#
# Example:
# python3 pc_make_raw_tensor_probe.py \
#   --kmodel /workspace/cubetest/best_AnchorBaseDet_can3_5_n_20260514232500.kmodel \
#   --image /workspace/cubetest/1s0b_0010.jpg \
#   --out-bin /workspace/cubetest/pc_input_1x3x480x640_uint8.bin

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


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--kmodel", required=True)
    p.add_argument("--image", required=True)
    p.add_argument("--width", type=int, default=640)
    p.add_argument("--height", type=int, default=480)
    p.add_argument("--out-bin", required=True)
    args = p.parse_args()

    print("=== PC raw tensor creation + simulator probe ===")
    print("kmodel:", args.kmodel)
    print("image :", args.image)
    print("out   :", args.out_bin)

    img = cv2.imread(args.image, cv2.IMREAD_COLOR)
    if img is None:
        raise FileNotFoundError(args.image)

    # This is the exact PC-side tensor pipeline you used earlier:
    # BGR jpg decode -> resize 640x480 -> RGB -> CHW -> batch -> uint8
    img = cv2.resize(img, (args.width, args.height), interpolation=cv2.INTER_LINEAR)
    img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
    inp = img.transpose(2, 0, 1)[None].astype(np.uint8)

    summarize("input", inp)

    # Save raw tensor bytes. Shape is always [1, 3, 480, 640].
    inp.tofile(args.out_bin)
    print("Saved raw uint8 tensor:", args.out_bin)
    print("Raw file size:", os.path.getsize(args.out_bin), "bytes")
    print("Expected size:", 1 * 3 * args.height * args.width, "bytes")

    sim = nc.Simulator()
    sim.load_model(open(args.kmodel, "rb").read())
    sim.set_input_tensor(0, nc.RuntimeTensor.from_numpy(inp))
    sim.run()

    try:
        n = sim.outputs_size()
    except Exception:
        n = 1

    print("Number of output tensors:", n)
    for i in range(n):
        out = sim.get_output_tensor(i).to_numpy()
        analyze_output(out, i)


if __name__ == "__main__":
    main()

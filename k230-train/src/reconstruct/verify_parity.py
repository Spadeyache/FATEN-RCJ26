"""Sanity-check the reconstructed model against the AI Cube baseline kmodel.

We can't expect bit-exact agreement -- the kmodel is uint8 PTQ-quantized and
might be the broken artifact itself. What we DO verify:

  1. PyTorch forward produces sensible value ranges on a real image.
  2. PyTorch == ONNX round-trip (via the export script).
  3. The spatial argmax of objectness in the reconstructed model is roughly
     where we'd expect a target to be, on a labeled test image.

For comparison with the kmodel:
  - The kmodel uses uint8 input with mean/std baked in.
  - We feed the same image through both pipelines and compare per-output
     tensors (statistics only, since quantization breaks exact equality).
"""
from __future__ import annotations

import argparse
import json
import os
import sys

import numpy as np


def _force_cpu_torch():
    import torch.serialization as ts
    ts.default_restore_location = lambda storage, _loc: storage
    if hasattr(ts, "_get_restore_location"):
        ts._get_restore_location = lambda *a, **kw: (lambda storage, _loc: storage)


sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))

import torch  # noqa: E402
_force_cpu_torch()

from reconstruct.build_anchorbasedet import AnchorBaseDet  # noqa: E402
from common.letterbox import letterbox_bgr, to_input_tensor  # noqa: E402


def load_reconstructed(pt_path: str, num_classes: int = 2):
    blob = torch.load(pt_path, map_location="cpu", weights_only=False)
    model = AnchorBaseDet(num_classes=num_classes)
    model.load_state_dict(blob["model_state_dict"], strict=True)
    model.eval()
    return model, blob


def normalize_imagenet(uint8_nchw: np.ndarray, mean, std) -> np.ndarray:
    """uint8 NCHW -> float32 NCHW in ImageNet-normalized space."""
    x = uint8_nchw.astype(np.float32) / 255.0
    m = np.array(mean, dtype=np.float32).reshape(1, 3, 1, 1)
    s = np.array(std, dtype=np.float32).reshape(1, 3, 1, 1)
    return (x - m) / s


def run_pytorch(model, normalized_nchw: np.ndarray):
    with torch.no_grad():
        outs = model(torch.from_numpy(normalized_nchw))
    return [o.cpu().numpy() for o in outs]


def run_kmodel(kmodel_path: str, uint8_nchw: np.ndarray):
    """Run the baseline kmodel via nncase Simulator. Returns 3 outputs."""
    import nncase as nc
    sim = nc.Simulator()
    with open(kmodel_path, "rb") as f:
        sim.load_model(f.read())
    sim.set_input_tensor(0, nc.RuntimeTensor.from_numpy(uint8_nchw))
    sim.run()
    outs = []
    for i in range(3):
        outs.append(sim.get_output_tensor(i).to_numpy())
    return outs


def stats(arr):
    return {
        "shape": list(arr.shape),
        "dtype": str(arr.dtype),
        "min": float(arr.min()),
        "max": float(arr.max()),
        "mean": float(arr.mean()),
        "std": float(arr.std()),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pt", default="data/anchorbasedet_reconstructed.pt")
    ap.add_argument("--kmodel", default="exports/v00_aicube_baseline/model.kmodel")
    ap.add_argument("--image",
                    help="Optional JPG for visual parity test")
    ap.add_argument("--input-width", type=int, default=640)
    ap.add_argument("--input-height", type=int, default=480)
    ap.add_argument("--report", default="reports/parity_report.json")
    args = ap.parse_args()

    model, blob = load_reconstructed(args.pt)
    mean = blob["mean"]
    std = blob["std"]
    print(f"Reconstructed PyTorch model loaded, mean={mean}, std={std}")

    # Build the input tensor.
    if args.image:
        import cv2
        bgr = cv2.imread(args.image, cv2.IMREAD_COLOR)
        if bgr is None:
            print(f"ERROR: cv2.imread failed for {args.image}", file=sys.stderr)
            sys.exit(2)
        letter, ratio, pad_lt = letterbox_bgr(bgr, args.input_width,
                                              args.input_height)
        uint8_nchw = to_input_tensor(letter, channel_order="rgb")
        print(f"Image: {args.image}  letterbox ratio={ratio:.3f}  pad={pad_lt}")
    else:
        # Synthetic mid-gray input (114 matches the letterbox fill).
        uint8_nchw = np.full((1, 3, args.input_height, args.input_width),
                             114, dtype=np.uint8)
        print("Using synthetic 114-fill input (no --image given)")

    normalized = normalize_imagenet(uint8_nchw, mean, std)

    # Run PyTorch.
    pt_outs = run_pytorch(model, normalized)
    print("\nPyTorch reconstructed (float32 with ImageNet normalization):")
    for i, o in enumerate(pt_outs):
        s = stats(o)
        print(f"  out[{i}]: shape={s['shape']} min={s['min']:.4g} "
              f"max={s['max']:.4g} mean={s['mean']:.4g} std={s['std']:.4g}")

    # Run kmodel (baseline AI Cube).
    km_outs = None
    km_error = None
    try:
        km_outs = run_kmodel(args.kmodel, uint8_nchw)
        print("\nAI Cube baseline .kmodel (uint8 input, baked preprocessing):")
        for i, o in enumerate(km_outs):
            s = stats(o)
            print(f"  out[{i}]: shape={s['shape']} min={s['min']:.4g} "
                  f"max={s['max']:.4g} mean={s['mean']:.4g} std={s['std']:.4g}")
    except Exception as e:
        km_error = f"{type(e).__name__}: {e}"
        print(f"\nnncase kmodel inference failed: {km_error}")
        print("Continuing -- parity vs kmodel is informational only.")

    # Compute per-output diffs if kmodel ran.
    diffs = []
    if km_outs is not None:
        print("\nNumerical comparison PyTorch (float) vs kmodel (quantized):")
        for i, (a, b) in enumerate(zip(pt_outs, km_outs)):
            if a.shape != b.shape:
                print(f"  out[{i}] SHAPE MISMATCH: pt={a.shape} km={b.shape}")
                diffs.append({"i": i, "shape_mismatch": True})
                continue
            d = np.abs(a - b)
            row = {
                "i": i,
                "shape": list(a.shape),
                "mean_abs": float(d.mean()),
                "max_abs": float(d.max()),
                "p95_abs": float(np.percentile(d, 95)),
            }
            diffs.append(row)
            print(f"  out[{i}]: mean|diff|={row['mean_abs']:.4g} "
                  f"max|diff|={row['max_abs']:.4g} "
                  f"p95|diff|={row['p95_abs']:.4g}")

    # Top objectness cells in PyTorch output (sanity check).
    print("\nTop-5 highest objectness cells (PyTorch out[0], stride 8):")
    o0 = pt_outs[0][0]                                   # (H, W, 21)
    obj_grid = o0.reshape(o0.shape[0], o0.shape[1], 3, 7)[..., 4]
    flat = obj_grid.reshape(-1)
    top5 = np.argpartition(-flat, 5)[:5]
    H, W = obj_grid.shape[:2]
    for idx in top5[np.argsort(-flat[top5])]:
        gy = (idx // 3) // W
        gx = (idx // 3) % W
        ai = idx % 3
        print(f"  gy={gy:3d} gx={gx:3d} anchor={ai} obj={float(flat[idx]):.4f}")

    out = {
        "mean": mean,
        "std": std,
        "input_shape": list(uint8_nchw.shape),
        "pytorch_stats": [stats(o) for o in pt_outs],
        "kmodel_stats": [stats(o) for o in km_outs] if km_outs is not None else None,
        "kmodel_error": km_error,
        "diffs": diffs,
    }
    os.makedirs(os.path.dirname(args.report) or ".", exist_ok=True)
    with open(args.report, "w") as f:
        json.dump(out, f, indent=2)
    print(f"\nSaved {args.report}")


if __name__ == "__main__":
    main()

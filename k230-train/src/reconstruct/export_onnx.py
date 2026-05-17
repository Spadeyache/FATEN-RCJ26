"""Export the reconstructed AnchorBaseDet to ONNX.

The ONNX input is float32 NCHW [1,3,480,640]. Preprocessing (ImageNet
mean/std) is NOT baked into the ONNX graph; nncase's CompileOptions
will handle that at compile time via `preprocess=True, mean=ImageNet,
std=ImageNet, input_range=[0,1]`.

The ONNX outputs are three NHWC tensors with sigmoid already applied:
  out[0] = (1, 60, 80, 21)   stride 8
  out[1] = (1, 30, 40, 21)   stride 16
  out[2] = (1, 15, 20, 21)   stride 32
"""
from __future__ import annotations

import argparse
import os
import sys


def _force_cpu_torch():
    import torch.serialization as ts
    ts.default_restore_location = lambda storage, _loc: storage
    if hasattr(ts, "_get_restore_location"):
        ts._get_restore_location = lambda *a, **kw: (lambda storage, _loc: storage)


sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))

import torch  # noqa: E402
_force_cpu_torch()

from reconstruct.build_anchorbasedet import AnchorBaseDet  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pt", default="data/anchorbasedet_reconstructed.pt")
    ap.add_argument("--out", default="data/anchorbasedet_reconstructed.onnx")
    ap.add_argument("--input-width", type=int, default=640)
    ap.add_argument("--input-height", type=int, default=480)
    ap.add_argument("--opset", type=int, default=11,
                    help="nncase 2.9 prefers opset 11-13.")
    ap.add_argument("--simplify", action="store_true",
                    help="Run onnxsim after export.")
    ap.add_argument("--num-classes", type=int, default=2)
    args = ap.parse_args()

    if not os.path.exists(args.pt):
        print(f"ERROR: not found: {args.pt}", file=sys.stderr)
        sys.exit(2)

    blob = torch.load(args.pt, map_location="cpu", weights_only=False)
    model = AnchorBaseDet(num_classes=args.num_classes)
    model.load_state_dict(blob["model_state_dict"], strict=True)
    model.eval()

    dummy = torch.zeros(1, 3, args.input_height, args.input_width,
                        dtype=torch.float32)

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    torch.onnx.export(
        model, dummy, args.out,
        input_names=["input"],
        output_names=["out0_s8", "out1_s16", "out2_s32"],
        opset_version=args.opset,
        do_constant_folding=True,
        dynamic_axes=None,
        export_params=True,
    )
    print(f"Saved ONNX -> {args.out} ({os.path.getsize(args.out):,} bytes)")

    if args.simplify:
        try:
            import onnx
            import onnxsim
            onnx_model = onnx.load(args.out)
            simplified, ok = onnxsim.simplify(onnx_model)
            assert ok, "onnxsim returned check=False"
            onnx.save(simplified, args.out)
            print(f"onnxsim done -> {args.out} "
                  f"({os.path.getsize(args.out):,} bytes)")
        except Exception as e:
            print(f"WARN: onnxsim failed: {e}")
            print("Continuing with un-simplified ONNX.")

    # Optional: verify in onnxruntime if available.
    try:
        import onnxruntime as ort
        sess = ort.InferenceSession(args.out, providers=["CPUExecutionProvider"])
        with torch.no_grad():
            pt_out = [o.numpy() for o in model(dummy)]
        ort_out = sess.run(None, {"input": dummy.numpy()})
        import numpy as np
        for i, (a, b) in enumerate(zip(pt_out, ort_out)):
            diff = float(np.abs(a - b).mean())
            print(f"  Pt vs ORT out[{i}]: mean|diff|={diff:.3e} "
                  f"(should be <1e-5)")
    except ImportError:
        print("onnxruntime not installed; skipping float parity check.")


if __name__ == "__main__":
    main()

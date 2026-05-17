"""Apply an edited quant_scheme.json (from diagnose_quant.py) to recompile
with selected layers promoted to int16.

Workflow:

  1. Run `diagnose_quant.py` to get `quant_scheme.json` with per-layer error.
  2. This script:
       - Reads the JSON.
       - Identifies the last `--promote-last-n` Conv layers (the detection
         heads) AND the top-K worst-cosine layers.
       - Sets their DataType to int16.
       - Writes the edited JSON.
       - Recompiles with `ptq.quant_scheme = edited_json` and the
         `use_mix_quant=False` workaround (Canaan's MixQuant has a
         duplicate-key crash in BindQuantMethodCosineImpl on YOLO-style
         heads, ref convert_canaan.py:225-232).

Usage:

    python3 apply_mix_quant.py \\
        --onnx /workspace/anchorbasedet_reconstructed.onnx \\
        --calib-dir /workspace/calibration_images \\
        --output-dir /workspace/exports/v09_raw01_mixquant \\
        --scheme /workspace/exports/v06_diag/dump/quant_scheme.json \\
        --promote-last-n 3 \\
        --promote-worst-k 5 \\
        --preprocess-mode baked_imagenet \\
        --ptq 3
"""
from __future__ import annotations

import argparse
import copy
import json
import math
import os
import sys
import time
from pathlib import Path


sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
from convert.convert_kmodel import (  # noqa: E402
    onnx_simplify, load_calibration_batch, preprocess_options, PTQ_OPTIONS,
)


def select_layers_to_promote(scheme: dict, promote_last_n: int,
                              promote_worst_k: int) -> set:
    """Return set of layer names that should be marked int16.

    nncase 2.9's quant_scheme.json has different shapes across point releases.
    We try a few common patterns:

      A) { "Outputs": [ {"Name": "...", "DataType": "...", "Error": x }, ... ] }
      B) [ {"Name": ..., "DataType": ..., "CosineError": x }, ... ]
      C) { "layers": [...] }

    We look at any list-of-dicts that has fields with a 'Name' or 'name'.
    """
    candidates = []
    if isinstance(scheme, list):
        candidates = scheme
    elif isinstance(scheme, dict):
        # find the first list-of-dicts value
        for v in scheme.values():
            if isinstance(v, list) and v and isinstance(v[0], dict):
                candidates = v
                break

    def name_of(layer): return layer.get("Name") or layer.get("name")
    def err_of(layer):
        for k in ("CosineError", "Error", "cosine_error", "error", "mse"):
            if k in layer:
                try:
                    return float(layer[k])
                except Exception:
                    pass
        return None

    names = [n for n in (name_of(l) for l in candidates) if n]

    promote = set()
    if promote_last_n > 0:
        promote.update(names[-promote_last_n:])

    if promote_worst_k > 0:
        # Sort by error descending (None = 0).
        scored = sorted(
            ((err_of(l) or 0.0, name_of(l)) for l in candidates),
            key=lambda t: -t[0],
        )
        for _, n in scored[:promote_worst_k]:
            if n:
                promote.add(n)
    return promote


def edit_scheme(scheme: dict, promote: set) -> dict:
    """Set DataType of `promote` layers to int16."""
    out = copy.deepcopy(scheme)

    def maybe_promote(layer):
        n = layer.get("Name") or layer.get("name")
        if n in promote:
            for k in ("DataType", "data_type", "dtype"):
                if k in layer:
                    layer[k] = "int16"
                    return
            layer.setdefault("DataType", "int16")

    if isinstance(out, list):
        for layer in out:
            if isinstance(layer, dict):
                maybe_promote(layer)
    elif isinstance(out, dict):
        for v in out.values():
            if isinstance(v, list):
                for layer in v:
                    if isinstance(layer, dict):
                        maybe_promote(layer)
    return out


def compile_with_scheme(onnx_path: str, output_dir: str, calib_dir: str,
                        input_width: int, input_height: int, calib_count: int,
                        ptq: int, preprocess_mode: str,
                        mean_imagenet, std_imagenet, swapRB: bool,
                        edited_scheme_path: str) -> dict:
    """Compile a kmodel using a quant_scheme.json that already encodes
    int16 promotions.

    nncase 2.9 has a bug with use_mix_quant=True on YOLO-style heads (duplicate
    key error in BindQuantMethodCosineImpl). Workaround: set use_mix_quant=False
    but still pass quant_scheme path; nncase reads dtypes from JSON anyway.
    """
    import nncase

    iw = int(math.ceil(input_width / 32.0)) * 32
    ih = int(math.ceil(input_height / 32.0)) * 32
    input_shape = [1, 3, ih, iw]
    calib_method, act_type, weight_type = PTQ_OPTIONS[ptq]
    pp = preprocess_options(preprocess_mode, mean_imagenet, std_imagenet)

    os.makedirs(output_dir, exist_ok=True)
    dump_dir = os.path.join(output_dir, "dump")
    os.makedirs(dump_dir, exist_ok=True)

    simplified = onnx_simplify(
        onnx_path, os.path.join(dump_dir, "simplified.onnx"), input_shape,
    )

    co = nncase.CompileOptions()
    co.target = "k230"
    co.preprocess = True
    co.swapRB = swapRB
    co.input_shape = input_shape
    co.input_type = "uint8"
    co.input_range = pp["input_range"]
    co.mean = pp["mean"]
    co.std = pp["std"]
    co.input_layout = "NCHW"
    co.output_layout = "NCHW"
    co.dump_dir = dump_dir

    compiler = nncase.Compiler(co)
    with open(simplified, "rb") as f:
        compiler.import_onnx(f.read(), nncase.ImportOptions())

    ptq_opts = nncase.PTQTensorOptions()
    ptq_opts.samples_count = calib_count
    ptq_opts.calibrate_method = calib_method
    ptq_opts.quant_type = act_type
    ptq_opts.w_quant_type = weight_type
    ptq_opts.set_tensor_data(
        load_calibration_batch(calib_dir, calib_count, iw, ih,
                               pp["calib_scale"])
    )

    # MixQuant flags
    if hasattr(ptq_opts, "use_mix_quant"):
        ptq_opts.use_mix_quant = False                # Canaan bug workaround
    if hasattr(ptq_opts, "quant_scheme"):
        ptq_opts.quant_scheme = edited_scheme_path
    if hasattr(ptq_opts, "quant_scheme_strict_mode"):
        ptq_opts.quant_scheme_strict_mode = False

    compiler.use_ptq(ptq_opts)
    t0 = time.time()
    compiler.compile()
    kbytes = compiler.gencode_tobytes()
    t = time.time() - t0

    kmodel_path = os.path.join(output_dir, "model.kmodel")
    with open(kmodel_path, "wb") as f:
        f.write(kbytes)

    out = {
        "kmodel_path": kmodel_path,
        "kmodel_size_bytes": len(kbytes),
        "compile_seconds": t,
        "quant_scheme_path": edited_scheme_path,
        "preprocess_mode": preprocess_mode,
        "ptq_code": ptq,
    }
    with open(os.path.join(output_dir, "summary.json"), "w") as f:
        json.dump(out, f, indent=2)
    print(f"[MIX] {kmodel_path}  size={len(kbytes):,} B  "
          f"compile={t:.1f}s")
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--onnx", required=True)
    ap.add_argument("--output-dir", required=True)
    ap.add_argument("--calib-dir", required=True)
    ap.add_argument("--scheme", required=True,
                    help="quant_scheme.json produced by diagnose_quant.py")
    ap.add_argument("--promote-last-n", type=int, default=3,
                    help="Number of trailing layers to mark int16")
    ap.add_argument("--promote-worst-k", type=int, default=5,
                    help="Number of worst-cosine layers to mark int16")
    ap.add_argument("--calib-count", type=int, default=16)
    ap.add_argument("--input-width", type=int, default=640)
    ap.add_argument("--input-height", type=int, default=480)
    ap.add_argument("--ptq", type=int, default=3, choices=list(PTQ_OPTIONS.keys()))
    ap.add_argument("--preprocess-mode", default="baked_imagenet")
    ap.add_argument("--mean", type=float, nargs=3,
                    default=[0.485, 0.456, 0.406])
    ap.add_argument("--std", type=float, nargs=3,
                    default=[0.229, 0.224, 0.225])
    ap.add_argument("--swapRB", action="store_true")
    args = ap.parse_args()

    if not os.path.exists(args.scheme):
        print(f"ERROR: not found: {args.scheme}", file=sys.stderr)
        sys.exit(2)

    with open(args.scheme) as f:
        scheme = json.load(f)
    promote = select_layers_to_promote(scheme, args.promote_last_n,
                                       args.promote_worst_k)
    print(f"Promoting {len(promote)} layers to int16:")
    for n in sorted(promote):
        print(f"  - {n}")

    edited = edit_scheme(scheme, promote)
    os.makedirs(args.output_dir, exist_ok=True)
    edited_path = os.path.join(args.output_dir, "quant_scheme_edited.json")
    with open(edited_path, "w") as f:
        json.dump(edited, f, indent=2)
    print(f"Edited scheme -> {edited_path}")

    compile_with_scheme(
        onnx_path=args.onnx,
        output_dir=args.output_dir,
        calib_dir=args.calib_dir,
        input_width=args.input_width,
        input_height=args.input_height,
        calib_count=args.calib_count,
        ptq=args.ptq,
        preprocess_mode=args.preprocess_mode,
        mean_imagenet=args.mean,
        std_imagenet=args.std,
        swapRB=args.swapRB,
        edited_scheme_path=edited_path,
    )


if __name__ == "__main__":
    main()

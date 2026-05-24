"""Diagnose per-layer quant error for MixQuant.

Runs a PTQ compile with `dump_quant_error=True` and `export_quant_scheme=True`
so nncase writes a `quant_scheme.json` listing every layer's quantization
configuration AND the per-layer cosine/MSE error it observed against the
float reference. The user (or apply_mix_quant.py) then edits the JSON to
promote the worst N layers to int16 and recompiles.

This is Canaan's recommended workflow per:
  https://www.kendryte.com/k230/en/dev/01_software/board/ai/K230_nncase_Development_Guide.html
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import time
from pathlib import Path

import numpy as np


sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
from convert.convert_kmodel import (  # noqa: E402
    onnx_simplify, load_calibration_batch, preprocess_options, PTQ_OPTIONS,
)


def diagnose(onnx_path: str, output_dir: str, calib_dir: str,
             input_width: int, input_height: int, calib_count: int,
             ptq: int, preprocess_mode: str,
             mean_imagenet, std_imagenet, swapRB: bool = False) -> dict:
    """Run the diagnostic compile. Returns paths + summary."""
    import math
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
    co.dump_ir = True
    co.dump_asm = True
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

    # DIAGNOSTIC FLAGS
    if hasattr(ptq_opts, "dump_quant_error"):
        ptq_opts.dump_quant_error = True
    if hasattr(ptq_opts, "export_quant_scheme"):
        ptq_opts.export_quant_scheme = True

    compiler.use_ptq(ptq_opts)
    t0 = time.time()
    compiler.compile()
    kbytes = compiler.gencode_tobytes()
    t = time.time() - t0

    kmodel_path = os.path.join(output_dir, "model.kmodel")
    with open(kmodel_path, "wb") as f:
        f.write(kbytes)

    # Find the exported quant scheme. nncase 2.9 writes it under dump_dir.
    candidates = list(Path(dump_dir).glob("**/quant_scheme*.json"))
    scheme_path = str(candidates[0]) if candidates else None

    print(f"[DIAG] kmodel  -> {kmodel_path}")
    print(f"[DIAG] dump    -> {dump_dir}")
    print(f"[DIAG] scheme  -> {scheme_path}")
    print(f"[DIAG] compile -> {t:.1f}s")

    out = {
        "kmodel_path": kmodel_path,
        "compile_seconds": t,
        "dump_dir": dump_dir,
        "quant_scheme_path": scheme_path,
        "ptq_code": ptq,
    }
    with open(os.path.join(output_dir, "summary.json"), "w") as f:
        json.dump(out, f, indent=2)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--onnx", required=True)
    ap.add_argument("--output-dir", required=True)
    ap.add_argument("--calib-dir", required=True)
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

    diagnose(
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
    )


if __name__ == "__main__":
    main()

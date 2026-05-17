"""Compile a FLOAT32 kmodel (no PTQ). Slower and bigger on K230D but
preserves model accuracy.

Diagnosis result: uint8 PTQ (and even MixQuant promoting many layers to
int16) destroys this AnchorBaseDet architecture's accuracy by ~99 %. The
float32 reconstructed ONNX achieves recall@0.05 = 1.00 vs ~0.07 for any
quantized variant. Float32 kmodel is the only deployable option short of
quantization-aware training.

Uses CompileOptions with `preprocess=True, input_type=uint8` so the K230D
still feeds raw uint8 from the camera and the kmodel internally dequantizes
+ normalizes. The COMPUTATIONS inside the kmodel are float32.

Usage:

    python3 convert_kmodel_float.py \\
        --onnx data/anchorbasedet_reconstructed.onnx \\
        --output-dir exports/v_float32

(No calibration data is needed.)
"""
from __future__ import annotations

import argparse
import json
import math
import os
import sys
import time
from pathlib import Path

import numpy as np


sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
from convert.convert_kmodel import (  # noqa: E402
    onnx_simplify, preprocess_options,
)


def compile_float(onnx_path: str, output_dir: str, input_width: int,
                  input_height: int, preprocess_mode: str,
                  mean_imagenet, std_imagenet, swapRB: bool = False,
                  dump: bool = False) -> dict:
    import nncase

    iw = int(math.ceil(input_width / 32.0)) * 32
    ih = int(math.ceil(input_height / 32.0)) * 32
    input_shape = [1, 3, ih, iw]
    pp = preprocess_options(preprocess_mode, mean_imagenet, std_imagenet)

    print(f"[CFG] FLOAT32 -- no PTQ")
    print(f"[CFG] onnx={onnx_path}")
    print(f"[CFG] input_shape={input_shape}")
    print(f"[CFG] preprocess_mode={preprocess_mode}: "
          f"range={pp['input_range']} mean={pp['mean']} std={pp['std']}")

    os.makedirs(output_dir, exist_ok=True)
    dump_dir = os.path.join(output_dir, "dump")
    os.makedirs(dump_dir, exist_ok=True)

    simplified = onnx_simplify(onnx_path,
                                os.path.join(dump_dir, "simplified.onnx"),
                                input_shape)

    co = nncase.CompileOptions()
    co.target = "k230"
    co.preprocess = True
    co.swapRB = swapRB
    co.input_shape = input_shape
    co.input_type = "uint8"                  # camera feeds uint8
    co.input_range = pp["input_range"]
    co.mean = pp["mean"]
    co.std = pp["std"]
    co.input_layout = "NCHW"
    co.output_layout = "NCHW"
    co.dump_ir = dump
    co.dump_asm = dump
    co.dump_dir = dump_dir

    compiler = nncase.Compiler(co)
    with open(simplified, "rb") as f:
        compiler.import_onnx(f.read(), nncase.ImportOptions())

    # CRITICAL: do NOT call compiler.use_ptq(...) -- that's what gives us
    # the broken uint8 kmodel.
    t0 = time.time()
    compiler.compile()
    kbytes = compiler.gencode_tobytes()
    t = time.time() - t0

    kmodel_path = os.path.join(output_dir, "model.kmodel")
    with open(kmodel_path, "wb") as f:
        f.write(kbytes)

    summary = {
        "kmodel_path": kmodel_path,
        "kmodel_size_bytes": len(kbytes),
        "compile_seconds": t,
        "input_shape": input_shape,
        "preprocess_mode": preprocess_mode,
        "input_range": pp["input_range"],
        "mean": pp["mean"],
        "std": pp["std"],
        "ptq_code": None,
        "calibrate_method": None,
        "quant_type": "float32",
        "w_quant_type": "float32",
        "calib_count": 0,
    }
    with open(os.path.join(output_dir, "summary.json"), "w") as f:
        json.dump(summary, f, indent=2)
    print(f"[OUT] {kmodel_path}  size={len(kbytes):,} B  "
          f"compile={t:.1f}s")
    return summary


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--onnx", required=True)
    ap.add_argument("--output-dir", required=True)
    ap.add_argument("--input-width", type=int, default=640)
    ap.add_argument("--input-height", type=int, default=480)
    ap.add_argument("--preprocess-mode", default="baked_imagenet",
                    choices=["baked_imagenet", "raw_01", "raw_0_255"])
    ap.add_argument("--mean", type=float, nargs=3,
                    default=[0.485, 0.456, 0.406])
    ap.add_argument("--std", type=float, nargs=3,
                    default=[0.229, 0.224, 0.225])
    ap.add_argument("--swapRB", action="store_true")
    ap.add_argument("--dump", action="store_true")
    args = ap.parse_args()

    compile_float(
        onnx_path=args.onnx, output_dir=args.output_dir,
        input_width=args.input_width, input_height=args.input_height,
        preprocess_mode=args.preprocess_mode,
        mean_imagenet=args.mean, std_imagenet=args.std,
        swapRB=args.swapRB, dump=args.dump,
    )


if __name__ == "__main__":
    main()

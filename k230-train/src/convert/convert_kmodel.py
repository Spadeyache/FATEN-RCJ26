"""ONNX -> K230 .kmodel via nncase 2.9.0 with explicit preprocessing options.

This module is designed to run INSIDE the user's Linux Docker container
(image: k230d-convert from ../k230-training/docker/convert/Dockerfile) where
both `nncase==2.9.0` AND `nncase-kpu==2.9.0` are available. The Windows
nncase install is missing the KPU plugin so we cannot compile / simulate
on the host.

Three preprocessing modes:

  baked_imagenet
    input_type=uint8, input_range=[0,1], mean=ImageNet, std=ImageNet
    On K230D you feed raw uint8 [0,255]; nncase internally does
    (uint8 / 255 - mean) / std.

  raw_01
    input_type=uint8, input_range=[0,1], mean=0, std=1
    On K230D you feed raw uint8 [0,255]; nncase divides by 255 only.

  raw_0_255
    input_type=uint8, input_range=[0,255], mean=0, std=[255,255,255]
    Mathematically equivalent to raw_01 but uses the [0,255] dequant form.

Usage (inside the convert container):

    python3 convert_kmodel.py \\
        --onnx /workspace/anchorbasedet_reconstructed.onnx \\
        --calib-dir /workspace/calibration_images \\
        --output-dir /workspace/exports/v04_raw01_kld_i16act_u8w \\
        --preprocess-mode baked_imagenet \\
        --ptq 4 \\
        --calib-count 16

The output dir gets:
  model.kmodel
  deploy_config.json    (copied/patched template)
  summary.json
  compile_options.txt   (verbatim CompileOptions)
  dump/                 (nncase IR dumps if --dump)
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


# ---------------------------------------------------------------------------
# PTQ option table (must match nncase_compat.PTQ_OPTIONS).
# ---------------------------------------------------------------------------
PTQ_OPTIONS = {
    0: ("NoClip", "uint8", "uint8"),
    1: ("NoClip", "uint8", "int16"),
    2: ("NoClip", "int16", "uint8"),
    3: ("Kld",    "uint8", "uint8"),
    4: ("Kld",    "uint8", "int16"),
    5: ("Kld",    "int16", "uint8"),
}


# ---------------------------------------------------------------------------
# Preprocessing presets that map to nncase CompileOptions.
# ---------------------------------------------------------------------------
def preprocess_options(mode: str, mean_imagenet, std_imagenet):
    if mode == "baked_imagenet":
        return {
            "input_range": [0.0, 1.0],
            "mean": list(map(float, mean_imagenet)),
            "std": list(map(float, std_imagenet)),
            "calib_scale": 1.0 / 255.0,   # calibration float values in [0,1]
        }
    if mode == "raw_01":
        return {
            "input_range": [0.0, 1.0],
            "mean": [0.0, 0.0, 0.0],
            "std": [1.0, 1.0, 1.0],
            "calib_scale": 1.0 / 255.0,
        }
    if mode == "raw_0_255":
        return {
            "input_range": [0.0, 255.0],
            "mean": [0.0, 0.0, 0.0],
            "std": [255.0, 255.0, 255.0],
            "calib_scale": 1.0,
        }
    raise ValueError(f"Unknown preprocess_mode: {mode}")


# ---------------------------------------------------------------------------
# ONNX simplify (matches Canaan's pattern).
# ---------------------------------------------------------------------------
def onnx_simplify(model_file: str, out_path: str, input_shape):
    import onnx
    import onnxsim
    m = onnx.load(model_file)
    m = onnx.shape_inference.infer_shapes(m)
    inputs = [n.name for n in m.graph.input
              if n.name not in {i.name for i in m.graph.initializer}]
    input_shapes = {n: list(input_shape) for n in inputs}
    try:
        m2, ok = onnxsim.simplify(m, overwrite_input_shapes=input_shapes)
    except TypeError:
        m2, ok = onnxsim.simplify(m, input_shapes=input_shapes)
    assert ok, "onnxsim returned check=False"
    onnx.save_model(m2, out_path)
    return out_path


# ---------------------------------------------------------------------------
# Calibration data loader.
# ---------------------------------------------------------------------------
def load_calibration_batch(calib_dir: str, count: int, width: int, height: int,
                           scale: float, use_letterbox: bool = True) -> list:
    """Return Canaan's expected format: list of [ndarray_NCHW_uint8].

    When CompileOptions has `preprocess=True` and `input_type="uint8"`,
    nncase expects RAW UINT8 calibration data. It internally applies the
    preprocess (dequant via input_range, then mean/std subtraction) during
    calibration to compute layer activation ranges in the post-preprocess
    distribution. We must NOT pre-apply `scale` here -- nncase does it.

    `use_letterbox=True` mirrors the K230 ai2d on-device path (114-pad).
    `use_letterbox=False` uses naive resize -- closer to how AI Cube
    probably trained, and avoids wasting calibration range on the
    constant 114 padding strip.
    """
    import cv2
    files = []
    for ext in ("*.jpg", "*.jpeg", "*.png", "*.bmp"):
        files.extend(sorted(Path(calib_dir).glob(ext)))
        files.extend(sorted(Path(calib_dir).glob(ext.upper())))
    if not files:
        raise FileNotFoundError(f"No images in {calib_dir}")
    if len(files) < count:
        files = (files * (count // len(files) + 1))[:count]
    else:
        files = files[:count]

    # `scale` is intentionally unused now (kept in the signature so callers
    # don't break; semantics are documented in the docstring).
    data = []
    for fp in files:
        bgr = cv2.imread(str(fp), cv2.IMREAD_COLOR)
        if bgr is None:
            raise IOError(f"cv2.imread failed: {fp}")
        if use_letterbox:
            ratio = min(width / bgr.shape[1], height / bgr.shape[0])
            new_w = int(round(bgr.shape[1] * ratio))
            new_h = int(round(bgr.shape[0] * ratio))
            resized = cv2.resize(bgr, (new_w, new_h),
                                 interpolation=cv2.INTER_LINEAR)
            top = (height - new_h) // 2
            bottom = height - new_h - top
            left = (width - new_w) // 2
            right = width - new_w - left
            letter = cv2.copyMakeBorder(resized, top, bottom, left, right,
                                        cv2.BORDER_CONSTANT,
                                        value=(114, 114, 114))
        else:
            # Naive resize -- distorts aspect but matches AI Cube training.
            letter = cv2.resize(bgr, (width, height),
                                interpolation=cv2.INTER_LINEAR)
        rgb = cv2.cvtColor(letter, cv2.COLOR_BGR2RGB)
        chw = rgb.transpose(2, 0, 1).astype(np.uint8)
        data.append([chw[np.newaxis, ...]])
    return data


# ---------------------------------------------------------------------------
# Compile.
# ---------------------------------------------------------------------------
def compile_one(onnx_path: str, output_dir: str, calib_dir: str,
                input_width: int, input_height: int, calib_count: int,
                ptq: int, preprocess_mode: str,
                mean_imagenet, std_imagenet,
                swapRB: bool = False, dump: bool = False,
                finetune_weights: str = "UseSquant",
                use_letterbox: bool = True) -> dict:
    """Compile one kmodel; returns a summary dict.

    finetune_weights: 'UseSquant' (AdaRound-like, default and recommended),
                      'UseAdaRound' (alternative), or 'NoFineTuneWeights'
                      (only useful as a baseline -- destroys MobileNet-style
                      backbones).
    use_letterbox:    True = ai2d-matching letterbox(114) padding for calib;
                      False = naive cv2.resize.
    """
    import nncase

    # K230 requires width/height divisible by 32.
    iw = int(math.ceil(input_width / 32.0)) * 32
    ih = int(math.ceil(input_height / 32.0)) * 32
    input_shape = [1, 3, ih, iw]
    calib_method, act_type, weight_type = PTQ_OPTIONS[ptq]
    pp = preprocess_options(preprocess_mode, mean_imagenet, std_imagenet)

    print(f"[CFG] onnx={onnx_path}")
    print(f"[CFG] input_shape={input_shape} (rounded to /32)")
    print(f"[CFG] target=k230, ptq={ptq} -> {calib_method} act={act_type} w={weight_type}")
    print(f"[CFG] preprocess_mode={preprocess_mode}: "
          f"range={pp['input_range']} mean={pp['mean']} std={pp['std']}")
    print(f"[CFG] swapRB={swapRB}, dump={dump}")
    print(f"[CFG] calib_dir={calib_dir}, calib_count={calib_count}, "
          f"use_letterbox={use_letterbox}")
    print(f"[CFG] finetune_weights_method={finetune_weights}")

    os.makedirs(output_dir, exist_ok=True)
    dump_dir = os.path.join(output_dir, "dump")
    os.makedirs(dump_dir, exist_ok=True)

    # ONNX simplify
    simplified = onnx_simplify(onnx_path, os.path.join(dump_dir, "simplified.onnx"),
                               input_shape)

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
    co.dump_ir = dump
    co.dump_asm = dump
    co.dump_dir = dump_dir

    compiler = nncase.Compiler(co)
    with open(simplified, "rb") as f:
        compiler.import_onnx(f.read(), nncase.ImportOptions())

    ptq_opts = nncase.PTQTensorOptions()
    ptq_opts.samples_count = calib_count
    ptq_opts.calibrate_method = calib_method
    ptq_opts.quant_type = act_type
    ptq_opts.w_quant_type = weight_type
    # Canaan's AdaRound -- critical for PTQ recovery on MobileNet-style nets.
    # Default in this project's convert_to_kmodel3.py:336 is "UseSquant".
    if hasattr(ptq_opts, "finetune_weights_method"):
        ptq_opts.finetune_weights_method = finetune_weights
    else:
        print(f"WARN: nncase build has no finetune_weights_method; ignoring "
              f"finetune_weights={finetune_weights}")
    ptq_opts.set_tensor_data(
        load_calibration_batch(calib_dir, calib_count, iw, ih,
                                pp["calib_scale"],
                                use_letterbox=use_letterbox)
    )
    compiler.use_ptq(ptq_opts)

    t0 = time.time()
    compiler.compile()
    kbytes = compiler.gencode_tobytes()
    compile_seconds = time.time() - t0

    kmodel_path = os.path.join(output_dir, "model.kmodel")
    with open(kmodel_path, "wb") as f:
        f.write(kbytes)
    size_bytes = len(kbytes)

    # Verbatim CompileOptions dump for forensic clarity.
    opts_txt = os.path.join(output_dir, "compile_options.txt")
    with open(opts_txt, "w") as f:
        f.write(f"target = {co.target}\n")
        f.write(f"preprocess = {co.preprocess}\n")
        f.write(f"swapRB = {co.swapRB}\n")
        f.write(f"input_shape = {co.input_shape}\n")
        f.write(f"input_type = {co.input_type}\n")
        f.write(f"input_range = {co.input_range}\n")
        f.write(f"mean = {co.mean}\n")
        f.write(f"std = {co.std}\n")
        f.write(f"input_layout = {co.input_layout}\n")
        f.write(f"output_layout = {co.output_layout}\n")
        f.write(f"\n# PTQ\n")
        f.write(f"calibrate_method = {ptq_opts.calibrate_method}\n")
        f.write(f"quant_type = {ptq_opts.quant_type}\n")
        f.write(f"w_quant_type = {ptq_opts.w_quant_type}\n")
        f.write(f"samples_count = {ptq_opts.samples_count}\n")
        f.write(f"finetune_weights_method = {finetune_weights}\n")
        f.write(f"use_letterbox = {use_letterbox}\n")

    summary = {
        "kmodel_path": kmodel_path,
        "kmodel_size_bytes": size_bytes,
        "compile_seconds": compile_seconds,
        "input_shape": input_shape,
        "preprocess_mode": preprocess_mode,
        "input_range": pp["input_range"],
        "mean": pp["mean"],
        "std": pp["std"],
        "ptq_code": ptq,
        "calibrate_method": calib_method,
        "quant_type": act_type,
        "w_quant_type": weight_type,
        "calib_count": calib_count,
        "finetune_weights_method": finetune_weights,
        "use_letterbox": use_letterbox,
    }
    with open(os.path.join(output_dir, "summary.json"), "w") as f:
        json.dump(summary, f, indent=2)

    print(f"[OUT] {kmodel_path}  size={size_bytes:,} B  "
          f"compile={compile_seconds:.1f}s")
    return summary


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--onnx", required=True)
    ap.add_argument("--output-dir", required=True)
    ap.add_argument("--calib-dir", required=True)
    ap.add_argument("--calib-count", type=int, default=16)
    ap.add_argument("--input-width", type=int, default=640)
    ap.add_argument("--input-height", type=int, default=480)
    ap.add_argument("--ptq", type=int, default=3, choices=list(PTQ_OPTIONS.keys()))
    ap.add_argument("--preprocess-mode", default="baked_imagenet",
                    choices=["baked_imagenet", "raw_01", "raw_0_255"])
    ap.add_argument("--mean", type=float, nargs=3,
                    default=[0.485, 0.456, 0.406])
    ap.add_argument("--std", type=float, nargs=3,
                    default=[0.229, 0.224, 0.225])
    ap.add_argument("--swapRB", action="store_true")
    ap.add_argument("--dump", action="store_true",
                    help="Enable nncase IR/asm dumps")
    args = ap.parse_args()

    if not os.path.exists(args.onnx):
        print(f"ERROR: not found: {args.onnx}", file=sys.stderr)
        sys.exit(2)
    if not os.path.isdir(args.calib_dir):
        print(f"ERROR: not a directory: {args.calib_dir}", file=sys.stderr)
        sys.exit(2)

    compile_one(
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
        dump=args.dump,
    )


if __name__ == "__main__":
    main()

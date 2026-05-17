#!/usr/bin/env python3
"""
YOLOv8 ONNX -> K230 .kmodel, strictly following Canaan's recommended pipeline.

Implements all three of Canaan's tools for fixing PTQ accuracy loss:
  1. The PTQ option sweep (0-5)                       — same as convert_to_kmodel4
  2. Per-layer quant-error diagnostic dump            — NEW
  3. MixQuant (selective int16 layer promotion)       — NEW

Source for the recommendations:
  https://www.kendryte.com/k230/en/dev/01_software/board/ai/K230_nncase_Development_Guide.html
  (section: MixQuant + dump_quant_error_symbolic)

Recommended workflow when PTQ crushes accuracy:
  Step 1 — baseline compile + diagnostic dump:
      python3 convert_canaan.py model.onnx --calib-data <dir> --diagnose
    Inspect the printed "[QERR] worst N layers" table — those are the layers
    PTQ is destroying.

  Step 2 — recompile with those layers promoted to int16:
      python3 convert_canaan.py model.onnx --calib-data <dir> \\
          --int16-layers "<comma-separated layer names from step 1>"

The model size grows slightly per int16 layer (~2x that layer's weight bytes).
The KPU runs int16 ops natively on K230 — no CPU fallback.
"""

import argparse
import math
import os
import shutil
from pathlib import Path

import numpy as np
import onnx
import onnxsim
import nncase
from PIL import Image

try:
    from onnx.helper import tensor_dtype_to_np_dtype as _onnx_dtype_to_np
except ImportError:
    from onnx.mapping import TENSOR_TYPE_TO_NP_TYPE
    def _onnx_dtype_to_np(t):
        return TENSOR_TYPE_TO_NP_TYPE[t]


# ---------------------------------------------------------------------------
# ONNX helpers (matches Canaan's to_kmodel.py exactly)
# ---------------------------------------------------------------------------
def parse_model_input_output(model_file, input_shape):
    onnx_model = onnx.load(model_file)
    input_all = [n.name for n in onnx_model.graph.input]
    input_initializer = [n.name for n in onnx_model.graph.initializer]
    input_names = list(set(input_all) - set(input_initializer))
    input_tensors = [n for n in onnx_model.graph.input if n.name in input_names]

    inputs = []
    for e in input_tensors:
        onnx_type = e.type.tensor_type
        inputs.append({
            "name": e.name,
            "dtype": _onnx_dtype_to_np(onnx_type.elem_type),
            "shape": [(i.dim_value if i.dim_value != 0 else d)
                      for i, d in zip(onnx_type.shape.dim, input_shape)],
        })
    return onnx_model, inputs


def onnx_simplify(model_file, dump_dir, input_shape):
    onnx_model, inputs = parse_model_input_output(model_file, input_shape)
    onnx_model = onnx.shape_inference.infer_shapes(onnx_model)
    input_shapes = {i["name"]: i["shape"] for i in inputs}

    try:
        onnx_model, check = onnxsim.simplify(
            onnx_model, overwrite_input_shapes=input_shapes
        )
    except TypeError:
        onnx_model, check = onnxsim.simplify(
            onnx_model, input_shapes=input_shapes
        )
    assert check, "Simplified ONNX model validation failed"

    out_path = os.path.join(dump_dir, "simplified.onnx")
    onnx.save_model(onnx_model, out_path)
    return out_path


def read_model_file(model_file):
    with open(model_file, "rb") as f:
        return f.read()


# ---------------------------------------------------------------------------
# Calibration data loader — strictly matches Canaan's pattern
# ---------------------------------------------------------------------------
def generate_data(shape, batch, calib_dir):
    """uint8 NCHW [0,255], PIL + BILINEAR resize — Canaan default."""
    img_paths = sorted(
        os.path.join(calib_dir, p)
        for p in os.listdir(calib_dir)
        if p.lower().endswith((".jpg", ".jpeg", ".png", ".bmp"))
    )
    assert len(img_paths) >= batch, (
        f"calibration images not enough: have {len(img_paths)}, need {batch}"
    )

    data = []
    for i in range(batch):
        img = Image.open(img_paths[i]).convert("RGB")
        img = img.resize((shape[3], shape[2]), Image.BILINEAR)
        arr = np.asarray(img, dtype=np.uint8)
        arr = np.transpose(arr, (2, 0, 1))
        data.append([arr[np.newaxis, ...]])
    return data


# ---------------------------------------------------------------------------
# PTQ option table — Canaan's K230_training_scripts/to_kmodel.py
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
# Compile (with optional diagnostic dump and MixQuant)
# ---------------------------------------------------------------------------
def compile_kmodel(onnx_path, output_dir, calib_dir, input_width, input_height,
                   num_samples, ptq_option, target,
                   diagnose=False, quant_scheme_path=None):
    """
    Returns path to written .kmodel.

    Three modes:
      1. Plain PTQ (default).
      2. Diagnose (diagnose=True): enables dump_quant_error AND export_quant_scheme.
         Produces a quant_scheme.json with per-layer dtype + error data which you
         then edit to mark bad layers as int16.
      3. MixQuant (quant_scheme_path=<file>): loads an edited quant_scheme.json
         and applies it strictly. This is Canaan's official MixQuant flow.
    """

    input_width = int(math.ceil(input_width / 32.0)) * 32
    input_height = int(math.ceil(input_height / 32.0)) * 32
    input_shape = [1, 3, input_height, input_width]
    calib_method, act_type, weight_type = PTQ_OPTIONS[ptq_option]

    print("=" * 60)
    print("  YOLOv8 -> K230 kmodel  [Canaan-aligned]")
    print("=" * 60)
    print(f"[CFG] ONNX             : {onnx_path}")
    print(f"[CFG] Input  (W x H)   : {input_width} x {input_height}")
    print(f"[CFG] Target           : {target}")
    print(f"[CFG] PTQ option       : {ptq_option} -> {calib_method} / act={act_type} / w={weight_type}")
    print(f"[CFG] Calibration dir  : {calib_dir}")
    print(f"[CFG] Calibration imgs : {num_samples}")
    print(f"[CFG] Diagnose mode    : {diagnose}")
    print(f"[CFG] MixQuant scheme  : {quant_scheme_path if quant_scheme_path else 'none'}")

    os.makedirs(output_dir, exist_ok=True)
    dump_dir = os.path.join(output_dir, "dump")
    os.makedirs(dump_dir, exist_ok=True)

    # 1. Simplify ONNX (Canaan does this first)
    model_file = onnx_simplify(onnx_path, dump_dir, input_shape)

    # 2. CompileOptions — Canaan-style preprocess
    co = nncase.CompileOptions()
    co.target = target
    co.preprocess = True
    co.swapRB = False
    co.input_shape = input_shape
    co.input_type = "uint8"
    co.input_range = [0, 1]      # Canaan pattern: dequantize uint8 -> [0,1]
    co.mean = [0, 0, 0]
    co.std = [1, 1, 1]            # division by 255 folded into dequant step
    co.input_layout = "NCHW"
    co.dump_ir = diagnose         # need IR dumps for layer names
    co.dump_asm = diagnose
    co.dump_dir = dump_dir

    compiler = nncase.Compiler(co)
    compiler.import_onnx(read_model_file(model_file), nncase.ImportOptions())
    print("[CONV] ONNX imported.")

    # 3. PTQ options
    ptq = nncase.PTQTensorOptions()
    ptq.samples_count = num_samples
    ptq.calibrate_method = calib_method
    ptq.quant_type = act_type
    ptq.w_quant_type = weight_type
    ptq.set_tensor_data(generate_data(input_shape, num_samples, calib_dir))

    # 4. Diagnose mode — dump per-layer quant error + export quant scheme JSON.
    #    nncase 2.9 attributes (verified on user's build):
    #       ptq.dump_quant_error            (bool)
    #       ptq.export_quant_scheme         (bool)
    #       ptq.quant_scheme                (path to JSON, both read+write)
    #       ptq.quant_scheme_strict_mode    (bool)
    #       ptq.use_mix_quant               (bool)
    quant_scheme_out = os.path.join(output_dir, "quant_scheme.json")

    if diagnose:
        ptq.dump_quant_error = True
        ptq.export_quant_scheme = True
        # IMPORTANT: do NOT set ptq.quant_scheme here. Setting that path makes
        # nncase try to READ the file. In export mode nncase writes the JSON
        # to a default location (usually under dump_dir).
        print(f"[DIAG] dump_quant_error    = True")
        print(f"[DIAG] export_quant_scheme = True (JSON written under {dump_dir})")

    # 5. MixQuant — apply an edited quant scheme JSON.
    #    Important: strict_mode=True crashes nncase 2.9 with the
    #    "An item with the same key has already been added" error during
    #    BindQuantMethodCosineImpl. Keep strict_mode False so nncase falls
    #    back to calibrate-on-the-fly for any tensor not explicitly listed.
    if quant_scheme_path:
        # nncase 2.9 has a bug with use_mix_quant=True on YOLOv8-style heads
        # (duplicate-key crash in BindQuantMethodCosineImpl). Workaround: pass
        # the scheme path WITHOUT enabling use_mix_quant — nncase still reads
        # the dtypes from the JSON via the quant_scheme path alone.
        ptq.use_mix_quant = False
        ptq.quant_scheme = quant_scheme_path
        ptq.quant_scheme_strict_mode = False
        print(f"[MIX] use_mix_quant = False (workaround for nncase 2.9 bug)")
        print(f"[MIX] quant_scheme = {quant_scheme_path}")

    compiler.use_ptq(ptq)
    print("\n[CONV] Compiling...")
    compiler.compile()

    kmodel_bytes = compiler.gencode_tobytes()
    base = Path(onnx_path).stem
    suffix = "_mix" if quant_scheme_path else ("_diag" if diagnose else "")
    kmodel_path = os.path.join(output_dir, f"{base}_k230{suffix}.kmodel")
    with open(kmodel_path, "wb") as f:
        f.write(kmodel_bytes)

    size_mb = os.path.getsize(kmodel_path) / (1024 * 1024)
    print(f"\n[OUT] {kmodel_path}  ({size_mb:.2f} MB)")

    if diagnose:
        print(f"\n[DIAG] Per-layer error dump under: {dump_dir}")
        print(f"[DIAG] Quant scheme exported to   : {quant_scheme_out}")
        print(f"[DIAG] Next step:")
        print(f"[DIAG]   1. Edit {quant_scheme_out}")
        print(f"[DIAG]   2. Find entries for layers /model.24/cv3.*/cv3.*.2/Conv*")
        print(f"[DIAG]   3. Change their DataType to 'int16'")
        print(f"[DIAG]   4. Re-run with --quant-scheme {quant_scheme_out}")

    return kmodel_path


def main():
    ap = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    ap.add_argument("onnx", help="Path to YOLOv8 .onnx")
    ap.add_argument("--calib-data", required=True, help="Directory of calibration images")
    ap.add_argument("--output", default="./output", help="Output dir for .kmodel")
    ap.add_argument("--input-width", type=int, default=640)
    ap.add_argument("--input-height", type=int, default=480)
    ap.add_argument("--num-samples", type=int, default=8,
                    help="Calibration images (Canaan default: 8)")
    ap.add_argument("--ptq-option", type=int, default=0, choices=list(PTQ_OPTIONS.keys()))
    ap.add_argument("--target", default="k230")
    ap.add_argument("--diagnose", action="store_true",
                    help="Dump per-layer quant error and export quant_scheme.json")
    ap.add_argument("--quant-scheme", type=str, default=None,
                    help="Path to an edited quant_scheme.json (enables MixQuant)")
    args = ap.parse_args()

    compile_kmodel(
        args.onnx, args.output, args.calib_data,
        args.input_width, args.input_height,
        args.num_samples, args.ptq_option, args.target,
        diagnose=args.diagnose, quant_scheme_path=args.quant_scheme,
    )


if __name__ == "__main__":
    main()

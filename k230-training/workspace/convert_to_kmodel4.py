#!/usr/bin/env python3
"""
YOLOv8 ONNX -> K230 .kmodel using Canaan's official preprocess pattern.

Key difference vs convert_to_kmodel3.py:
    input_range = [0, 1]      (was [0, 255])
    std         = [1, 1, 1]   (was [255, 255, 255])

Both are mathematically equivalent to "divide by 255", but nncase folds the
[0,1] form into the uint8 dequantization step itself, which preserves
classification-head precision much better. This is the pattern from
Canaan's K230_training_scripts/end2end_det_doc/to_kmodel.py.

Usage:
    python3 convert_to_kmodel4.py /models/best_640x480.onnx \\
        --calib-data /datasets/my_dataset/train/images \\
        --num-samples 8 \\
        --ptq-option 0 \\
        --output /workspace/output

ptq_option values (from Canaan):
    0 = NoClip  + uint8 act + uint8 weights      (default; try first)
    1 = NoClip  + uint8 act + int16 weights
    2 = NoClip  + int16 act + uint8 weights
    3 = Kld     + uint8 act + uint8 weights
    4 = Kld     + uint8 act + int16 weights
    5 = Kld     + int16 act + uint8 weights
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

# Handle onnx version differences for dtype lookup (only used for safety; not critical)
try:
    from onnx.helper import tensor_dtype_to_np_dtype as _onnx_dtype_to_np
except ImportError:
    from onnx.mapping import TENSOR_TYPE_TO_NP_TYPE
    def _onnx_dtype_to_np(t):
        return TENSOR_TYPE_TO_NP_TYPE[t]


# ---------------------------------------------------------------------------
# ONNX helpers
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

    # onnxsim API differs by version: newer uses overwrite_input_shapes, older uses input_shapes
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
# Calibration data
# ---------------------------------------------------------------------------
def generate_data(shape, batch, calib_dir):
    """Returns list of [np.ndarray] per sample. uint8 NCHW [0,255]."""
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
        arr = np.transpose(arr, (2, 0, 1))          # HWC -> CHW
        data.append([arr[np.newaxis, ...]])         # add batch dim
    return data


# ---------------------------------------------------------------------------
# Main convert
# ---------------------------------------------------------------------------
def convert(onnx_path, output_dir, calib_dir, input_width, input_height,
            num_samples, ptq_option, target):

    # round up to multiples of 32 (KPU requirement)
    input_width = int(math.ceil(input_width / 32.0)) * 32
    input_height = int(math.ceil(input_height / 32.0)) * 32
    input_shape = [1, 3, input_height, input_width]

    print(f"{'='*60}")
    print(f"  YOLOv8 -> K230 kmodel  (convert_to_kmodel4 - Canaan-style)")
    print(f"{'='*60}")
    print(f"[CFG] ONNX             : {onnx_path}")
    print(f"[CFG] Input  (W x H)   : {input_width} x {input_height}")
    print(f"[CFG] Target           : {target}")
    print(f"[CFG] PTQ option       : {ptq_option}")
    print(f"[CFG] Calibration dir  : {calib_dir}")
    print(f"[CFG] Calibration imgs : {num_samples}")

    os.makedirs(output_dir, exist_ok=True)
    dump_dir = os.path.join(output_dir, "tmp")
    if not os.path.exists(dump_dir):
        os.makedirs(dump_dir)

    # 1. simplify ONNX
    model_file = onnx_simplify(onnx_path, dump_dir, input_shape)

    # 2. compile options - CANAAN PATTERN
    co = nncase.CompileOptions()
    co.target = target
    co.preprocess = True
    co.swapRB = False
    co.input_shape = input_shape
    co.input_type = "uint8"
    co.input_range = [0, 1]          # <-- key difference vs convert_to_kmodel3
    co.mean = [0, 0, 0]
    co.std = [1, 1, 1]                # <-- key difference vs convert_to_kmodel3
    co.input_layout = "NCHW"
    co.dump_ir = False
    co.dump_asm = False
    co.dump_dir = dump_dir

    print(f"\n[CFG] input_range      : {co.input_range}")
    print(f"[CFG] mean / std       : {co.mean} / {co.std}")

    # 3. compile
    compiler = nncase.Compiler(co)
    model_content = read_model_file(model_file)
    compiler.import_onnx(model_content, nncase.ImportOptions())
    print("[CONV] ONNX imported.")

    # 4. PTQ options
    ptq = nncase.PTQTensorOptions()
    ptq.samples_count = num_samples

    if ptq_option == 0:
        ptq.calibrate_method = "NoClip"
        ptq.quant_type, ptq.w_quant_type = "uint8", "uint8"
    elif ptq_option == 1:
        ptq.calibrate_method = "NoClip"
        ptq.quant_type, ptq.w_quant_type = "uint8", "int16"
    elif ptq_option == 2:
        ptq.calibrate_method = "NoClip"
        ptq.quant_type, ptq.w_quant_type = "int16", "uint8"
    elif ptq_option == 3:
        ptq.calibrate_method = "Kld"
        ptq.quant_type, ptq.w_quant_type = "uint8", "uint8"
    elif ptq_option == 4:
        ptq.calibrate_method = "Kld"
        ptq.quant_type, ptq.w_quant_type = "uint8", "int16"
    elif ptq_option == 5:
        ptq.calibrate_method = "Kld"
        ptq.quant_type, ptq.w_quant_type = "int16", "uint8"
    else:
        raise ValueError(f"Unknown ptq_option: {ptq_option}")

    print(f"[PTQ] calibrate_method : {ptq.calibrate_method}")
    print(f"[PTQ] quant_type       : {ptq.quant_type}")
    print(f"[PTQ] w_quant_type     : {ptq.w_quant_type}")
    print(f"[PTQ] samples_count    : {ptq.samples_count}")

    ptq.set_tensor_data(generate_data(input_shape, num_samples, calib_dir))
    compiler.use_ptq(ptq)

    print("\n[CONV] Compiling... (1-5 min)")
    compiler.compile()

    kmodel_bytes = compiler.gencode_tobytes()
    base = Path(onnx_path).stem
    kmodel_path = os.path.join(output_dir, f"{base}_k230.kmodel")
    with open(kmodel_path, "wb") as f:
        f.write(kmodel_bytes)

    size_mb = os.path.getsize(kmodel_path) / (1024 * 1024)
    print(f"\n[OUT] {kmodel_path}  ({size_mb:.2f} MB)")

    # cleanup
    shutil.rmtree(dump_dir, ignore_errors=True)
    return kmodel_path


def main():
    ap = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    ap.add_argument("onnx", help="Path to YOLOv8 .onnx")
    ap.add_argument("--calib-data", required=True, help="Directory of calibration images")
    ap.add_argument("--output", default="./output", help="Output dir for .kmodel")
    ap.add_argument("--input-width", type=int, default=640)
    ap.add_argument("--input-height", type=int, default=480)
    ap.add_argument("--num-samples", type=int, default=8,
                    help="Calibration images to use (Canaan default: 8)")
    ap.add_argument("--ptq-option", type=int, default=0, choices=[0, 1, 2, 3, 4, 5])
    ap.add_argument("--target", default="k230")
    args = ap.parse_args()

    convert(args.onnx, args.output, args.calib_data,
            args.input_width, args.input_height,
            args.num_samples, args.ptq_option, args.target)


if __name__ == "__main__":
    main()

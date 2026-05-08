#!/usr/bin/env python3
"""
YOLOv8 ONNX → K230D .kmodel (nncase 2.9.0). Self-contained; does not import convert_to_kmodel2.

K230D calibration (ai2d / KPU path):
  Float32 with pixel values in [0, 255] and **no** /255 normalization is required so the
  deployed model matches how the K230D **ai2d** block feeds the KPU (hardware preprocessing).
  nncase PTQ also expects float32 tensors aligned with the ONNX graph input (f32), not uint8.

  (K230D専用: float32 かつ /255 しない設定は、ai2d のハードウェア前処理と量子化後の実機挙動を一致させるために必須。)
"""

import argparse
import gc
import os
import sys
from pathlib import Path

import numpy as np
import onnx


def verify_onnx_model(onnx_path: str, img_height: int, img_width: int) -> dict:
    print(f"\n{'='*60}")
    print(f"[ONNX] Verifying model: {onnx_path}")
    print(f"{'='*60}")

    model = onnx.load(onnx_path)
    onnx.checker.check_model(model)
    print(f"[ONNX] ✓ Model is valid")
    print(f"[ONNX]   IR version : {model.ir_version}")
    print(f"[ONNX]   Opset      : {model.opset_import[0].version}")

    info = {"inputs": [], "outputs": []}

    for inp in model.graph.input:
        shape = [d.dim_value for d in inp.type.tensor_type.shape.dim]
        dtype = inp.type.tensor_type.elem_type
        print(f"[ONNX]   Input  '{inp.name}': shape={shape}, dtype_id={dtype}")
        if len(shape) == 4:
            _, c, h, w = shape
            if h != img_height or w != img_width:
                print(f"[ONNX]   ⚠  ONNX input ({w}x{h}) ≠ target ({img_width}x{img_height})")
            else:
                print(f"[ONNX]   ✓ Input size matches target {img_width}x{img_height}")
            if c != 3:
                print(f"[ONNX]   ⚠  Expected 3 channels, got {c}")
            else:
                print(f"[ONNX]   ✓ Channels = 3 (RGB)")
        info["inputs"].append({"name": inp.name, "shape": shape})

    for out in model.graph.output:
        shape = [d.dim_value for d in out.type.tensor_type.shape.dim]
        print(f"[ONNX]   Output '{out.name}': shape={shape}")
        if len(shape) == 3:
            _, rows, anchors = shape
            num_classes = rows - 4
            print(
                f"[ONNX]   ✓ YOLOv8 output: {anchors} anchors, "
                f"4 box coords + {num_classes} classes"
            )
        info["outputs"].append({"name": out.name, "shape": shape})

    return info


def generate_calibration_data(
    dataset_path: str,
    img_height: int = 480,
    img_width: int = 640,
    num_samples: int = 100,
) -> list:
    """
    Build PTQ calibration: float32 NCHW, values [0, 255], no /255 — matches K230D ai2d + ONNX f32.
    """
    import cv2
    from glob import glob

    print(f"\n{'='*60}")
    print(f"[CALIB] Generating calibration data (float32 [0,255], NO /255)  [K230D ai2d-compatible]")
    print(f"{'='*60}")
    print(f"[CALIB]   Source        : {dataset_path}")
    print(f"[CALIB]   Target size   : {img_width}x{img_height} (WxH)")
    print(f"[CALIB]   Max samples   : {num_samples}")
    print(f"[CALIB]   Value range   : [0, 255] float32  (nncase PTQ + ONNX f32 input)")

    image_files = []
    for ext in ["*.jpg", "*.jpeg", "*.png", "*.bmp", "*.webp"]:
        found = glob(os.path.join(dataset_path, "**", ext), recursive=True)
        image_files.extend(found)

    if not image_files:
        raise ValueError(f"[CALIB] No images found in: {dataset_path}")

    image_files = sorted(image_files)[:num_samples]
    print(f"[CALIB]   Images found  : {len(image_files)}")

    calibration_data = []
    failed = 0

    for idx, img_path in enumerate(image_files):
        try:
            img = cv2.imread(img_path)
            if img is None:
                print(f"[CALIB]   ⚠  Could not read: {img_path}")
                failed += 1
                continue

            original_h, original_w = img.shape[:2]
            img = cv2.resize(img, (img_width, img_height))
            img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
            # Hardware expects uint8 [0, 255]. Compiler preprocess handles 0-1 scaling internally.
            img = img.astype(np.uint8)

            img = np.transpose(img, (2, 0, 1))
            img = np.expand_dims(img, axis=0)

            if idx < 3:
                print(f"\n[CALIB]   Sample {idx}: {os.path.basename(img_path)}")
                print(f"[CALIB]     Original  : {original_w}x{original_h}")
                print(f"[CALIB]     Shape     : {img.shape}  (NCHW)")
                print(f"[CALIB]     dtype     : {img.dtype}")
                print(f"[CALIB]     min/max   : {img.min():.0f} / {img.max():.0f}  ← should be ~0 / ~255")
                print(f"[CALIB]     mean      : {img.mean():.1f}  ← should be ~100-150")

            calibration_data.append(img)

        except Exception as exc:
            print(f"[CALIB]   ⚠  Failed ({img_path}): {exc}")
            failed += 1

    if not calibration_data:
        raise ValueError("[CALIB] No calibration data could be generated.")

    all_data = np.concatenate(calibration_data, axis=0)
    print(f"\n[CALIB] ── Summary ──────────────────────────────────────")
    print(f"[CALIB]   Loaded ok     : {len(calibration_data)}")
    print(f"[CALIB]   Failed        : {failed}")
    print(f"[CALIB]   Batch shape   : {all_data.shape}  (N,C,H,W)")
    print(f"[CALIB]   dtype         : {all_data.dtype}")
    print(f"[CALIB]   Global min    : {all_data.min():.0f}  (expect ≈ 0)")
    print(f"[CALIB]   Global max    : {all_data.max():.0f}  (expect ≈ 255)")
    print(f"[CALIB]   Global mean   : {all_data.mean():.1f}  (expect ~100-150)")
    print(f"[CALIB] ────────────────────────────────────────────────────")

    del all_data
    return calibration_data


def generate_random_calibration(img_height: int, img_width: int, n: int = 20) -> list:
    print(f"\n[CALIB] ⚠  Using RANDOM calibration data (not recommended for production)")
    print(f"[CALIB]   Samples : {n}")
    print(f"[CALIB]   Range   : [0, 255] uint8")
    data = [
        (np.random.randint(0, 256, (1, 3, img_height, img_width), dtype=np.uint8))
        for _ in range(n)
    ]
    print(f"[CALIB]   Sample[0] min/max: {data[0].min():.0f} / {data[0].max():.0f}")
    return data


def convert_to_kmodel(
    onnx_path: str,
    output_dir: str = "./output",
    calibration_data_path: str = None,
    img_height: int = 480,
    img_width: int = 640,
    target: str = "k230",
    num_calibration_samples: int = 100,
    quant_type: str = "uint8",
    w_quant_type: str = "uint8",
    calib_method: str = "Kld",
) -> str:

    print(f"\n{'='*60}")
    print(f"  YOLOv8 → K230D kmodel  (nncase 2.9.0)  [convert_to_kmodel3]")
    print(f"  Calibration: float32 [0,255] — NO /255 (matches ONNX f32 input)")
    print(f"{'='*60}")

    if not os.path.exists(onnx_path):
        raise FileNotFoundError(f"ONNX model not found: {onnx_path}")

    os.makedirs(output_dir, exist_ok=True)

    model_name = Path(onnx_path).stem
    kmodel_path = os.path.join(output_dir, f"{model_name}_k230.kmodel")

    print(f"\n[CONFIG] Input ONNX      : {onnx_path}")
    print(f"[CONFIG] Output kmodel   : {kmodel_path}")
    print(f"[CONFIG] Target          : {target}")
    print(f"[CONFIG] Input size      : {img_width}x{img_height} (WxH)")
    print(f"[CONFIG] Act quant type  : {quant_type}")
    print(f"[CONFIG] Weight quant    : {w_quant_type}")
    print(f"[CONFIG] Calib method    : {calib_method}")
    print(f"[CONFIG] Calib samples   : {num_calibration_samples}")

    verify_onnx_model(onnx_path, img_height, img_width)

    try:
        import nncase
        version = getattr(nncase, "__version__", "unknown")
        print(f"\n[NNCASE] ✓ nncase version: {version}")
    except ImportError:
        raise ImportError(
            "nncase not installed.\n"
            "  pip install nncase==2.9.0 nncase-kpu==2.9.0"
        )

    if calibration_data_path:
        calibration_data = generate_calibration_data(
            calibration_data_path,
            img_height=img_height,
            img_width=img_width,
            num_samples=num_calibration_samples,
        )
    else:
        print("\n[CALIB] ⚠  No --calib-data provided.")
        calibration_data = generate_random_calibration(img_height, img_width, n=20)

    print(f"\n{'='*60}")
    print(f"[COMPILE] Starting compilation...")
    print(f"{'='*60}")

    try:
        compile_options = nncase.CompileOptions()
        compile_options.target = target
        compile_options.dump_ir = False
        compile_options.dump_asm = False
        compile_options.dump_dir = os.path.join(output_dir, "dump")
        print(f"[COMPILE] ✓ CompileOptions configured (target={target})")

        compile_options.preprocess = True
        compile_options.swapRB = False
        compile_options.input_type = "uint8"
        compile_options.input_shape = [1, 3, img_height, img_width]
        compile_options.input_layout = "NCHW"
        compile_options.output_layout = "NCHW"
        compile_options.input_range = [0, 255]
        compile_options.mean = [0, 0, 0]
        compile_options.std = [255.0, 255.0, 255.0]
        
        print(f"[COMPILE] preprocess : {compile_options.preprocess}")

        ptq_options = nncase.PTQTensorOptions()
        ptq_options.quant_type = quant_type
        ptq_options.w_quant_type = w_quant_type
        ptq_options.calibrate_method = calib_method
        ptq_options.finetune_weights_method = "NoFineTuneWeights"
        ptq_options.samples_count = len(calibration_data)

        print(f"[COMPILE] ✓ PTQTensorOptions:")
        print(f"[COMPILE]     quant_type       : {ptq_options.quant_type}")
        print(f"[COMPILE]     w_quant_type     : {ptq_options.w_quant_type}")
        print(f"[COMPILE]     calibrate_method : {ptq_options.calibrate_method}")
        print(f"[COMPILE]     samples_count    : {ptq_options.samples_count}")

        ptq_data = [[img] for img in calibration_data]
        print(f"\n[COMPILE] Calibration tensor check:")
        print(f"[COMPILE]   ptq_data length    : {len(ptq_data)}")
        print(f"[COMPILE]   sample shape       : {ptq_data[0][0].shape}")
        print(f"[COMPILE]   sample dtype       : {ptq_data[0][0].dtype}")
        print(
            f"[COMPILE]   sample min/max     : {ptq_data[0][0].min():.0f} / {ptq_data[0][0].max():.0f}  ← must be [0, 255]"
        )

        ptq_options.set_tensor_data(ptq_data)
        compile_options.quant_options = ptq_options
        print(f"[COMPILE] ✓ Calibration data set")

        compiler = nncase.Compiler(compile_options)
        print(f"[COMPILE] ✓ Compiler created")

        import_options = nncase.ImportOptions()
        with open(onnx_path, "rb") as f:
            model_content = f.read()
        print(f"\n[COMPILE] Importing ONNX ({len(model_content)/1024/1024:.2f} MB)...")
        compiler.import_onnx(model_content, import_options)
        print(f"[COMPILE] ✓ ONNX imported")

        compiler.use_ptq(ptq_options)
        print(f"[COMPILE] ✓ PTQ applied")

        print(f"\n[COMPILE] Compiling... (this may take several minutes)")
        compiler.compile()
        print(f"[COMPILE] ✓ Compilation complete")

        kmodel_bytes = compiler.gencode_tobytes()
        print(f"[COMPILE] ✓ kmodel generated ({len(kmodel_bytes)/1024/1024:.2f} MB)")

        with open(kmodel_path, "wb") as f:
            f.write(kmodel_bytes)

        size_mb = os.path.getsize(kmodel_path) / (1024 * 1024)
        print(f"\n{'='*60}")
        print(f"[OUTPUT] ✓ Saved to  : {kmodel_path}")
        print(f"[OUTPUT] ✓ File size : {size_mb:.2f} MB")

        if size_mb > 64:
            print(f"[OUTPUT] ⚠  Model is large — verify it fits in K230D RAM")
        else:
            print(f"[OUTPUT] ✓ Size OK for K230D deployment")

        gc.collect()
        return kmodel_path

    except Exception as exc:
        print(f"\n[COMPILE] ✗ Compilation failed: {exc}")
        raise


def main():
    parser = argparse.ArgumentParser(
        description="Convert YOLOv8 ONNX → K230D kmodel (nncase 2.9.0, standalone)",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("onnx", type=str, help="Path to YOLOv8 .onnx model")
    parser.add_argument("--output", type=str, default="./output", help="Output directory for .kmodel")
    parser.add_argument("--calib-data", type=str, required=True, help="Path to calibration images")
    parser.add_argument("--img-height", type=int, default=480, help="Model input height")
    parser.add_argument("--img-width", type=int, default=640, help="Model input width")
    parser.add_argument("--num-samples", type=int, default=100, help="Number of calibration images")
    parser.add_argument("--target", type=str, default="k230", help="nncase target device")
    parser.add_argument("--quant-type", type=str, default="uint8", choices=["uint8", "int8"])
    parser.add_argument("--w-quant-type", type=str, default="uint8", choices=["uint8", "int8"])
    parser.add_argument(
        "--calib-method", type=str, default="Kld", choices=["Kld", "NoClip", "AbsMax"]
    )

    args = parser.parse_args()

    kmodel_path = convert_to_kmodel(
        onnx_path=args.onnx,
        output_dir=args.output,
        calibration_data_path=args.calib_data,
        img_height=args.img_height,
        img_width=args.img_width,
        target=args.target,
        num_calibration_samples=args.num_samples,
        quant_type=args.quant_type,
        w_quant_type=args.w_quant_type,
        calib_method=args.calib_method,
    )
    print(f"\n✓ Done! kmodel ready at: {kmodel_path}\n")


if __name__ == "__main__":
    main()

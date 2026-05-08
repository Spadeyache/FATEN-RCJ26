#!/usr/bin/env python3
"""
Export YOLOv8n to ONNX format compatible with K230D nncase
Optimizes for 320x320 input and removes NMS for KPU inference
"""

import os
import sys
import gc
from pathlib import Path
from ultralytics import YOLO
import onnx
import onnxsim
import torch


def export_to_onnx(
    model_path: str,
    output_dir: str = "/models",
    img_height: int = 480,
    img_width: int = 640,
    opset: int = 11,
    simplify: bool = True,
    dynamic: bool = False
):
    """
    Export YOLOv8n model to ONNX format for K230D
    
    Args:
        model_path: Path to trained .pt model
        output_dir: Output directory for ONNX model
        img_height: Input image height (must match training)
        img_width: Input image width (must match training)
        opset: ONNX opset version (11 or 12 recommended for nncase)
        simplify: Simplify ONNX model
        dynamic: Use dynamic input shapes (False for K230D)
    
    Returns:
        Path to exported ONNX model
    """
    
    print("=" * 80)
    print("Exporting YOLOv8n to ONNX for K230D")
    print("=" * 80)
    
    # Verify model exists
    if not os.path.exists(model_path):
        raise FileNotFoundError(f"Model not found: {model_path}")
    
    # Create output directory
    os.makedirs(output_dir, exist_ok=True)
    
    # Load model
    print(f"\nLoading model: {model_path}")
    model = YOLO(model_path)
    
    # Generate output filename
    model_name = Path(model_path).stem
    onnx_filename = f"{model_name}_{img_width}x{img_height}.onnx"
    onnx_path = os.path.join(output_dir, onnx_filename)
    
    print(f"\nExport Configuration:")
    print(f"  - Input Size: {img_width}x{img_height}")
    print(f"  - ONNX Opset: {opset}")
    print(f"  - Dynamic Shapes: {dynamic}")
    print(f"  - Simplify: {simplify}")
    print(f"  - Output: {onnx_path}")
    
    # Export to ONNX
    print("\n" + "=" * 80)
    print("Exporting to ONNX...")
    print("=" * 80 + "\n")
    
    # Export with specific settings for K230D
    model.export(
        format="onnx",
        imgsz=(img_height, img_width),
        opset=opset,
        dynamic=dynamic,
        simplify=False,  # We'll do this manually for better control
    )
    
    # The export creates the file in the same directory as the model
    # Move it to our output directory
    exported_onnx = str(Path(model_path).parent / f"{model_name}.onnx")
    
    if os.path.exists(exported_onnx):
        # Load and check the model
        print("Loading ONNX model for verification...")
        onnx_model = onnx.load(exported_onnx)
        
        # Simplify the model if requested
        if simplify:
            print("\nSimplifying ONNX model...")
            try:
                onnx_model, check = onnxsim.simplify(
                    onnx_model,
                    dynamic_input_shape=dynamic,
                    input_shapes={f"images": [1, 3, img_height, img_width]} if not dynamic else None
                )
                print(f"  ✓ Simplification successful: {check}")
            except Exception as e:
                print(f"  ⚠ Simplification failed: {e}")
                print("  Continuing with original model...")
        
        # Save the final model
        print(f"\nSaving ONNX model to: {onnx_path}")
        onnx.save(onnx_model, onnx_path)
        
        # Clean up temporary file
        if exported_onnx != onnx_path and os.path.exists(exported_onnx):
            os.remove(exported_onnx)
        
        # Print model info
        print("\n" + "=" * 80)
        print("ONNX Model Information")
        print("=" * 80)
        print(f"\nInputs:")
        for input_tensor in onnx_model.graph.input:
            shape = [dim.dim_value for dim in input_tensor.type.tensor_type.shape.dim]
            print(f"  - {input_tensor.name}: {shape}")
        
        print(f"\nOutputs:")
        for output_tensor in onnx_model.graph.output:
            shape = [dim.dim_value for dim in output_tensor.type.tensor_type.shape.dim]
            print(f"  - {output_tensor.name}: {shape}")
        
        print(f"\nModel size: {os.path.getsize(onnx_path) / (1024*1024):.2f} MB")
        
        print("\n" + "=" * 80)
        print("Export Complete!")
        print("=" * 80)
        print(f"\n✓ ONNX model saved: {onnx_path}")
        print(f"✓ Next step: Convert to .kmodel using convert_to_kmodel.py")
        
        # Clean up
        gc.collect()
        if torch.cuda.is_available():
            torch.cuda.empty_cache()
        
        return onnx_path
    else:
        raise RuntimeError(f"ONNX export failed. Expected file not found: {exported_onnx}")


if __name__ == "__main__":
    import argparse
    
    parser = argparse.ArgumentParser(description="Export YOLOv8n to ONNX for K230D")
    parser.add_argument("model", type=str, help="Path to trained .pt model")
    parser.add_argument("--output", type=str, default="/models", help="Output directory")
    parser.add_argument("--img-height", type=int, default=480, help="Image height")
    parser.add_argument("--img-width", type=int, default=640, help="Image width")
    parser.add_argument("--opset", type=int, default=11, help="ONNX opset version")
    parser.add_argument("--no-simplify", action="store_true", help="Skip ONNX simplification")
    parser.add_argument("--dynamic", action="store_true", help="Use dynamic input shapes")
    
    args = parser.parse_args()
    
    onnx_path = export_to_onnx(
        model_path=args.model,
        output_dir=args.output,
        img_height=args.img_height,
        img_width=args.img_width,
        opset=args.opset,
        simplify=not args.no_simplify,
        dynamic=args.dynamic
    )
    
    print(f"\n✓ Export complete: {onnx_path}")

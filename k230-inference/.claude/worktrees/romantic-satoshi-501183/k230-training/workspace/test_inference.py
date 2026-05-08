#!/usr/bin/env python3
"""
Test inference on sample images
Useful for validating model before K230D deployment
"""

import os
import sys
import cv2
import numpy as np
from pathlib import Path
from glob import glob


def test_pytorch_model(model_path, test_images_dir, output_dir, conf=0.25):
    """Test PyTorch model inference"""
    from ultralytics import YOLO
    
    print("=" * 80)
    print("Testing PyTorch Model")
    print("=" * 80)
    
    # Load model
    print(f"\nLoading model: {model_path}")
    model = YOLO(model_path)
    
    # Find test images
    image_files = []
    for ext in ['*.jpg', '*.jpeg', '*.png']:
        image_files.extend(glob(os.path.join(test_images_dir, ext)))
    
    if not image_files:
        print(f"✗ No test images found in {test_images_dir}")
        return False
    
    print(f"Found {len(image_files)} test images")
    
    # Create output directory
    os.makedirs(output_dir, exist_ok=True)
    
    # Run inference
    print("\nRunning inference...")
    for img_path in image_files[:10]:  # Test first 10 images
        print(f"  Processing: {Path(img_path).name}")
        
        # Run prediction
        results = model(img_path, conf=conf, imgsz=320)
        
        # Save result
        for r in results:
            output_path = os.path.join(output_dir, f"result_{Path(img_path).name}")
            r.save(output_path)
            
            # Print detections
            boxes = r.boxes
            if len(boxes) > 0:
                print(f"    Detections: {len(boxes)}")
                for box in boxes:
                    cls = int(box.cls[0])
                    conf = float(box.conf[0])
                    print(f"      - Class {cls}: {conf:.2f}")
            else:
                print(f"    No detections")
    
    print(f"\n✓ Results saved to: {output_dir}")
    return True


def test_onnx_model(onnx_path, test_images_dir, output_dir, img_size=320):
    """Test ONNX model inference"""
    import onnxruntime as ort
    
    print("=" * 80)
    print("Testing ONNX Model")
    print("=" * 80)
    
    # Load ONNX model
    print(f"\nLoading ONNX model: {onnx_path}")
    try:
        session = ort.InferenceSession(onnx_path)
        print(f"✓ Model loaded successfully")
        
        # Print model info
        input_name = session.get_inputs()[0].name
        input_shape = session.get_inputs()[0].shape
        print(f"  Input: {input_name}, shape: {input_shape}")
        
        for i, output in enumerate(session.get_outputs()):
            print(f"  Output {i}: {output.name}, shape: {output.shape}")
        
    except Exception as e:
        print(f"✗ Failed to load ONNX model: {e}")
        return False
    
    # Find test images
    image_files = []
    for ext in ['*.jpg', '*.jpeg', '*.png']:
        image_files.extend(glob(os.path.join(test_images_dir, ext)))
    
    if not image_files:
        print(f"✗ No test images found in {test_images_dir}")
        return False
    
    print(f"\nFound {len(image_files)} test images")
    
    # Create output directory
    os.makedirs(output_dir, exist_ok=True)
    
    # Test inference
    print("\nRunning inference...")
    for img_path in image_files[:5]:  # Test first 5 images
        print(f"  Processing: {Path(img_path).name}")
        
        try:
            # Read and preprocess image
            img = cv2.imread(img_path)
            img_resized = cv2.resize(img, (img_size, img_size))
            img_rgb = cv2.cvtColor(img_resized, cv2.COLOR_BGR2RGB)
            img_normalized = img_rgb.astype(np.float32) / 255.0
            img_transposed = np.transpose(img_normalized, (2, 0, 1))
            img_batch = np.expand_dims(img_transposed, axis=0)
            
            # Run inference
            outputs = session.run(None, {input_name: img_batch})
            
            # Print output shapes
            print(f"    Output shapes: {[o.shape for o in outputs]}")
            
            # Check for detections (simplified)
            if outputs[0].size > 0:
                print(f"    ✓ Inference successful")
            
        except Exception as e:
            print(f"    ✗ Inference failed: {e}")
    
    print(f"\n✓ ONNX model testing complete")
    return True


def main():
    import argparse
    
    parser = argparse.ArgumentParser(description="Test model inference")
    parser.add_argument("model", type=str, help="Path to model (.pt or .onnx)")
    parser.add_argument("--test-images", type=str, default="/test_images",
                        help="Directory with test images")
    parser.add_argument("--output", type=str, default="/output/test_results",
                        help="Output directory for results")
    parser.add_argument("--conf", type=float, default=0.25,
                        help="Confidence threshold")
    parser.add_argument("--img", type=int, default=320,
                        help="Image size")
    
    args = parser.parse_args()
    
    # Check model type
    model_ext = Path(args.model).suffix.lower()
    
    if model_ext == '.pt':
        success = test_pytorch_model(
            args.model,
            args.test_images,
            args.output,
            args.conf
        )
    elif model_ext == '.onnx':
        success = test_onnx_model(
            args.model,
            args.test_images,
            args.output,
            args.img
        )
    else:
        print(f"✗ Unsupported model format: {model_ext}")
        print("  Supported: .pt, .onnx")
        sys.exit(1)
    
    if success:
        print("\n✓ Testing complete!")
    else:
        print("\n✗ Testing failed")
        sys.exit(1)


if __name__ == "__main__":
    main()

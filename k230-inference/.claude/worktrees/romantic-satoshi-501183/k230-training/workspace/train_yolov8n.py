#!/usr/bin/env python3
"""
YOLOv8n Training Script for K230D Deployment
Trains at 320x320 resolution for optimal memory usage on 128MB RAM
"""

import os
import gc
import yaml
from pathlib import Path
from ultralytics import YOLO
import torch

def train_yolov8n(
    data_yaml: str = "data.yaml",
    epochs: int = 100,
    batch_size: int = 16,
    img_height: int = 480,  # Default raw input height
    img_width: int = 640,   # Default raw input width
    project: str = "/runs",
    name: str = "yolov8n_k230d",
    device: str = "0",
    workers: int = 8,
    cache: bool = False,
    pretrained: bool = True
):
    """
    Train YOLOv8n model optimized for K230D deployment
    
    Args:
        data_yaml: Path to dataset configuration
        epochs: Number of training epochs
        batch_size: Training batch size
        img_height: Input image height (e.g., 480 for 640x480)
        img_width: Input image width (e.g., 640 for 640x480)
        project: Project directory for saving runs
        name: Run name
        device: CUDA device (e.g., '0' or 'cpu')
        workers: Number of data loading workers
        cache: Cache images in RAM
        pretrained: Use pretrained weights
    """
    
    print("=" * 80)
    print(f"YOLOv8n Training for K230D ({img_width}x{img_height} Resolution)")
    print("=" * 80)
    
    # Verify data.yaml exists
    if not os.path.exists(data_yaml):
        raise FileNotFoundError(f"Dataset config not found: {data_yaml}")
    
    # Load and verify dataset config
    with open(data_yaml, 'r') as f:
        data_config = yaml.safe_load(f)
    
    print(f"\nDataset Configuration:")
    print(f"  - Train: {data_config.get('train', 'N/A')}")
    print(f"  - Val: {data_config.get('val', 'N/A')}")
    print(f"  - Classes: {data_config.get('nc', 'N/A')}")
    print(f"  - Names: {data_config.get('names', 'N/A')}")
    
    # Initialize model
    model_name = "yolov8n.pt" if pretrained else "yolov8n.yaml"
    print(f"\nInitializing model: {model_name}")
    model = YOLO(model_name)
    
    # Training configuration
    print(f"\nTraining Configuration:")
    print(f"  - Image Size: {img_width}x{img_height}")
    print(f"  - Batch Size: {batch_size}")
    print(f"  - Epochs: {epochs}")
    print(f"  - Device: {device}")
    print(f"  - Workers: {workers}")
    
    # Train the model
    print("\n" + "=" * 80)
    print("Starting Training...")
    print("=" * 80 + "\n")
    
    results = model.train(
        data=data_yaml,
        epochs=epochs,
        imgsz=(img_height, img_width),
        batch=batch_size,
        device=device,
        workers=workers,
        project=project,
        name=name,
        cache=cache,
        # Optimization settings for faster training
        amp=True,  # Automatic Mixed Precision
        patience=50,
        save=True,
        save_period=10,
        # Augmentation (moderate for better convergence)
        hsv_h=0.015,
        hsv_s=0.7,
        hsv_v=0.4,
        degrees=0.0,
        translate=0.1,
        scale=0.5,
        shear=0.0,
        perspective=0.0,
        flipud=0.0,
        fliplr=0.5,
        mosaic=1.0,
        mixup=0.0,
        copy_paste=0.0,
    )
    
    print("\n" + "=" * 80)
    print("Training Complete!")
    print("=" * 80)
    
    # Get best model path
    best_model_path = Path(project) / name / "weights" / "best.pt"
    last_model_path = Path(project) / name / "weights" / "last.pt"
    
    print(f"\nModel saved at:")
    print(f"  - Best: {best_model_path}")
    print(f"  - Last: {last_model_path}")
    
    # Validate the best model
    print("\n" + "=" * 80)
    print("Validating Best Model...")
    print("=" * 80 + "\n")
    
    best_model = YOLO(str(best_model_path))
    metrics = best_model.val(data=data_yaml, imgsz=(img_height, img_width), device=device)
    
    print(f"\nValidation Results:")
    print(f"  - mAP50: {metrics.box.map50:.4f}")
    print(f"  - mAP50-95: {metrics.box.map:.4f}")
    print(f"  - Precision: {metrics.box.mp:.4f}")
    print(f"  - Recall: {metrics.box.mr:.4f}")
    
    # Clean up
    gc.collect()
    if torch.cuda.is_available():
        torch.cuda.empty_cache()
    
    return str(best_model_path)


if __name__ == "__main__":
    import argparse
    
    parser = argparse.ArgumentParser(description="Train YOLOv8n for K230D")
    parser.add_argument("--data", type=str, default="data.yaml", help="Path to data.yaml")
    parser.add_argument("--epochs", type=int, default=100, help="Number of epochs")
    parser.add_argument("--batch", type=int, default=16, help="Batch size")
    parser.add_argument("--img-height", type=int, default=480, help="Image height (default: 480)")
    parser.add_argument("--img-width", type=int, default=640, help="Image width (default: 640)")
    parser.add_argument("--project", type=str, default="/runs", help="Project directory")
    parser.add_argument("--name", type=str, default="yolov8n_k230d", help="Run name")
    parser.add_argument("--device", type=str, default="0", help="CUDA device")
    parser.add_argument("--workers", type=int, default=8, help="Number of workers")
    parser.add_argument("--cache", action="store_true", help="Cache images")
    parser.add_argument("--no-pretrained", action="store_true", help="Train from scratch")
    
    args = parser.parse_args()
    
    best_model = train_yolov8n(
        data_yaml=args.data,
        epochs=args.epochs,
        batch_size=args.batch,
        img_height=args.img_height,
        img_width=args.img_width,
        project=args.project,
        name=args.name,
        device=args.device,
        workers=args.workers,
        cache=args.cache,
        pretrained=not args.no_pretrained
    )
    
    print(f"\n✓ Training complete! Best model: {best_model}")
    print(f"✓ Next step: Export to ONNX using export_to_onnx.py")

#!/usr/bin/env python3
"""
YOLOv5n Training Script for K230 Deployment.

Why YOLOv5n instead of YOLOv8n:
  Canaan's official K230 PTQ pipeline is tuned for the YOLOv5 anchor-based head.
  YOLOv8's anchor-free decoupled head causes PTQ in nncase 2.9 to crush the
  classification scores. YOLOv5n's anchor-based head quantizes cleanly with the
  same toolchain.

Same data, same training infrastructure as train_yolov8n.py. Just a different
base architecture. Reuses your existing data.yaml.
"""

import os
import gc
import yaml
from pathlib import Path
from ultralytics import YOLO
import torch


def train_yolov5n(
    data_yaml: str = "data.yaml",
    epochs: int = 100,
    batch_size: int = 16,
    img_height: int = 480,
    img_width: int = 640,
    project: str = "/runs",
    name: str = "yolov5n_k230",
    device: str = "0",
    workers: int = 8,
    cache: bool = False,
    pretrained: bool = True,
):
    """
    Train YOLOv5n model optimized for K230 deployment.

    Args same as train_yolov8n.py; only the base architecture differs.
    """

    print("=" * 80)
    print(f"YOLOv5n Training for K230 ({img_width}x{img_height} Resolution)")
    print("  (anchor-based head — quantizes cleanly with nncase 2.9 PTQ)")
    print("=" * 80)

    if not os.path.exists(data_yaml):
        raise FileNotFoundError(f"Dataset config not found: {data_yaml}")

    with open(data_yaml, "r") as f:
        data_config = yaml.safe_load(f)

    print(f"\nDataset Configuration:")
    print(f"  - Train  : {data_config.get('train', 'N/A')}")
    print(f"  - Val    : {data_config.get('val', 'N/A')}")
    print(f"  - Classes: {data_config.get('nc', 'N/A')}")
    print(f"  - Names  : {data_config.get('names', 'N/A')}")

    # ---------------------------------------------------------------
    # IMPORTANT: use yolov5n.pt (anchor-based, original v5).
    # Do NOT use yolov5nu.pt — that's the anchor-free port and has the
    # same PTQ-unfriendly head as YOLOv8. We specifically want the
    # original anchor-based head here.
    # ---------------------------------------------------------------
    model_name = "yolov5n.pt" if pretrained else "yolov5n.yaml"
    print(f"\nInitializing model: {model_name}")
    model = YOLO(model_name)

    print(f"\nTraining Configuration:")
    print(f"  - Image Size : {img_width}x{img_height}")
    print(f"  - Batch Size : {batch_size}")
    print(f"  - Epochs     : {epochs}")
    print(f"  - Device     : {device}")
    print(f"  - Workers    : {workers}")

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
        amp=True,
        patience=50,
        save=True,
        save_period=10,
        # Same augmentation as v8 — proven to work on this dataset
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

    best_model_path = Path(project) / name / "weights" / "best.pt"
    last_model_path = Path(project) / name / "weights" / "last.pt"

    print(f"\nModel saved at:")
    print(f"  - Best: {best_model_path}")
    print(f"  - Last: {last_model_path}")

    print("\n" + "=" * 80)
    print("Validating Best Model...")
    print("=" * 80 + "\n")

    best_model = YOLO(str(best_model_path))
    metrics = best_model.val(
        data=data_yaml, imgsz=(img_height, img_width), device=device
    )

    print(f"\nValidation Results:")
    print(f"  - mAP50    : {metrics.box.map50:.4f}")
    print(f"  - mAP50-95 : {metrics.box.map:.4f}")
    print(f"  - Precision: {metrics.box.mp:.4f}")
    print(f"  - Recall   : {metrics.box.mr:.4f}")

    gc.collect()
    if torch.cuda.is_available():
        torch.cuda.empty_cache()

    return str(best_model_path)


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Train YOLOv5n for K230")
    parser.add_argument("--data", type=str, default="data.yaml")
    parser.add_argument("--epochs", type=int, default=100)
    parser.add_argument("--batch", type=int, default=16)
    parser.add_argument("--img-height", type=int, default=480)
    parser.add_argument("--img-width", type=int, default=640)
    parser.add_argument("--project", type=str, default="/runs")
    parser.add_argument("--name", type=str, default="yolov5n_k230")
    parser.add_argument("--device", type=str, default="0")
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--cache", action="store_true")
    parser.add_argument("--no-pretrained", action="store_true")

    args = parser.parse_args()

    best_model = train_yolov5n(
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
        pretrained=not args.no_pretrained,
    )

    print(f"\n✓ Training complete! Best model: {best_model}")
    print(f"✓ Next step: Export to ONNX using export_to_onnx.py")

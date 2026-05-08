#!/usr/bin/env python3
"""
Validate YOLO dataset structure and configuration
Checks for common issues before training
"""

import os
import sys
import yaml
from pathlib import Path
from glob import glob


def validate_yaml(yaml_path):
    """Validate data.yaml configuration"""
    print("=" * 80)
    print("Validating Dataset Configuration")
    print("=" * 80)
    
    if not os.path.exists(yaml_path):
        print(f"✗ Error: {yaml_path} not found")
        return False
    
    with open(yaml_path, 'r') as f:
        config = yaml.safe_load(f)
    
    # Check required fields
    required_fields = ['path', 'train', 'val', 'nc', 'names']
    missing_fields = [f for f in required_fields if f not in config]
    
    if missing_fields:
        print(f"✗ Missing required fields: {missing_fields}")
        return False
    
    print(f"\n✓ data.yaml structure valid")
    print(f"  - Dataset path: {config['path']}")
    print(f"  - Number of classes: {config['nc']}")
    print(f"  - Classes: {config['names']}")
    
    return config


def validate_paths(config):
    """Validate dataset paths exist"""
    print("\n" + "=" * 80)
    print("Validating Dataset Paths")
    print("=" * 80)
    
    base_path = config['path']
    train_path = os.path.join(base_path, config['train'])
    val_path = os.path.join(base_path, config['val'])
    
    # Check paths exist
    paths_ok = True
    
    if not os.path.exists(base_path):
        print(f"✗ Base path not found: {base_path}")
        paths_ok = False
    else:
        print(f"✓ Base path exists: {base_path}")
    
    if not os.path.exists(train_path):
        print(f"✗ Training images not found: {train_path}")
        paths_ok = False
    else:
        print(f"✓ Training images found: {train_path}")
    
    if not os.path.exists(val_path):
        print(f"✗ Validation images not found: {val_path}")
        paths_ok = False
    else:
        print(f"✓ Validation images found: {val_path}")
    
    return paths_ok, train_path, val_path


def count_images(image_dir):
    """Count images in directory"""
    extensions = ['*.jpg', '*.jpeg', '*.png', '*.bmp', '*.JPG', '*.JPEG', '*.PNG']
    images = []
    for ext in extensions:
        images.extend(glob(os.path.join(image_dir, ext)))
        images.extend(glob(os.path.join(image_dir, '**', ext), recursive=True))
    return len(images), images


def count_labels(label_dir):
    """Count label files"""
    labels = glob(os.path.join(label_dir, '*.txt'))
    labels.extend(glob(os.path.join(label_dir, '**', '*.txt'), recursive=True))
    return len(labels), labels


def validate_dataset_size(config, train_path, val_path):
    """Validate dataset has sufficient images"""
    print("\n" + "=" * 80)
    print("Validating Dataset Size")
    print("=" * 80)
    
    # Count training images
    train_count, train_images = count_images(train_path)
    print(f"\nTraining images: {train_count}")
    
    if train_count == 0:
        print("✗ No training images found!")
        return False
    elif train_count < 50:
        print("⚠ Warning: Very small training set (< 50 images)")
        print("  Consider collecting more data for better results")
    elif train_count < 100:
        print("⚠ Warning: Small training set (< 100 images)")
    else:
        print("✓ Sufficient training images")
    
    # Count validation images
    val_count, val_images = count_images(val_path)
    print(f"\nValidation images: {val_count}")
    
    if val_count == 0:
        print("✗ No validation images found!")
        return False
    elif val_count < 10:
        print("⚠ Warning: Very small validation set (< 10 images)")
    else:
        print("✓ Sufficient validation images")
    
    # Check for labels
    print("\n" + "-" * 80)
    print("Checking Labels")
    print("-" * 80)
    
    # Infer label directories
    train_label_dir = train_path.replace('images', 'labels')
    val_label_dir = val_path.replace('images', 'labels')
    
    if os.path.exists(train_label_dir):
        train_label_count, _ = count_labels(train_label_dir)
        print(f"Training labels: {train_label_count}")
        
        if train_label_count < train_count:
            print(f"⚠ Warning: Fewer labels ({train_label_count}) than images ({train_count})")
        else:
            print("✓ Label count matches or exceeds image count")
    else:
        print(f"✗ Training label directory not found: {train_label_dir}")
        return False
    
    if os.path.exists(val_label_dir):
        val_label_count, _ = count_labels(val_label_dir)
        print(f"Validation labels: {val_label_count}")
        
        if val_label_count < val_count:
            print(f"⚠ Warning: Fewer labels ({val_label_count}) than images ({val_count})")
        else:
            print("✓ Label count matches or exceeds image count")
    else:
        print(f"✗ Validation label directory not found: {val_label_dir}")
        return False
    
    return True


def sample_label_check(config, train_path):
    """Check a sample label file for format issues"""
    print("\n" + "=" * 80)
    print("Sample Label Validation")
    print("=" * 80)
    
    train_label_dir = train_path.replace('images', 'labels')
    labels = glob(os.path.join(train_label_dir, '*.txt'))
    labels.extend(glob(os.path.join(train_label_dir, '**', '*.txt'), recursive=True))
    
    if not labels:
        print("✗ No labels found to sample")
        return False
    
    # Check first label file
    sample_label = labels[0]
    print(f"\nChecking: {sample_label}")
    
    try:
        with open(sample_label, 'r') as f:
            lines = f.readlines()
        
        if not lines:
            print("⚠ Warning: Label file is empty")
            return True  # Empty is valid (no objects)
        
        print(f"  Annotations: {len(lines)}")
        
        # Check first annotation
        parts = lines[0].strip().split()
        if len(parts) != 5:
            print(f"✗ Invalid format: Expected 5 values, got {len(parts)}")
            print(f"  Format should be: class x_center y_center width height")
            return False
        
        # Validate values
        try:
            cls = int(parts[0])
            x, y, w, h = map(float, parts[1:])
            
            if cls >= config['nc']:
                print(f"✗ Invalid class ID: {cls} (max: {config['nc']-1})")
                return False
            
            if not (0 <= x <= 1 and 0 <= y <= 1 and 0 <= w <= 1 and 0 <= h <= 1):
                print(f"✗ Coordinates out of range [0, 1]: x={x}, y={y}, w={w}, h={h}")
                return False
            
            print(f"✓ Sample label format valid")
            print(f"  Class: {cls} ({config['names'][cls]})")
            print(f"  Bbox: x={x:.3f}, y={y:.3f}, w={w:.3f}, h={h:.3f}")
            
        except ValueError as e:
            print(f"✗ Invalid values in label: {e}")
            return False
        
    except Exception as e:
        print(f"✗ Error reading label: {e}")
        return False
    
    return True


def main(yaml_path='data.yaml'):
    """Run all validation checks"""
    print("\n" + "=" * 80)
    print("K230D YOLOv8n Dataset Validator")
    print("=" * 80 + "\n")
    
    # Validate YAML
    config = validate_yaml(yaml_path)
    if not config:
        print("\n✗ Validation failed: Invalid data.yaml")
        return False
    
    # Validate paths
    paths_ok, train_path, val_path = validate_paths(config)
    if not paths_ok:
        print("\n✗ Validation failed: Invalid paths")
        return False
    
    # Validate dataset size
    if not validate_dataset_size(config, train_path, val_path):
        print("\n✗ Validation failed: Dataset issues")
        return False
    
    # Sample label check
    if not sample_label_check(config, train_path):
        print("\n⚠ Warning: Label format issues detected")
        print("  Please verify your label files are in YOLO format")
    
    # Summary
    print("\n" + "=" * 80)
    print("Validation Summary")
    print("=" * 80)
    print("\n✓ Dataset validation complete!")
    print("✓ Your dataset is ready for training")
    print("\nNext step: Run training with:")
    print(f"  python train_yolov8n.py --data {yaml_path}")
    
    return True


if __name__ == "__main__":
    import argparse
    
    parser = argparse.ArgumentParser(description="Validate YOLO dataset")
    parser.add_argument("--data", type=str, default="data.yaml",
                        help="Path to data.yaml")
    
    args = parser.parse_args()
    
    success = main(args.data)
    sys.exit(0 if success else 1)

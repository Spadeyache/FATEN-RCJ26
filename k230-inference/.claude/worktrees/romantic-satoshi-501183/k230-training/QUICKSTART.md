# 🚀 Quick Start Guide - K230D YOLOv8n

Get up and running in 5 minutes!

## Prerequisites

- Docker & Docker Compose installed
- NVIDIA GPU with CUDA support (for training)
- Your dataset in YOLO format

## Step-by-Step

### 1. Setup (1 minute)

```bash
cd k230dev-yolo

# Create directories and config
make setup

# Edit your dataset configuration
nano workspace/data.yaml
# Update: path, nc (number of classes), names (class names)
```

### 2. Build Docker Images (5-10 minutes)

```bash
# Build both training and conversion containers
make build
```

Or use pre-built images:
```bash
docker compose pull  # If available
```

### 3. Train Model (varies: 30 min - 2 hours)

```bash
# Start training
docker compose run --rm train python train_yolov8n.py \
    --data data.yaml \
    --epochs 100 \
    --batch 16 \
    --img 320 \
    --name my_first_model

# For quick test (10 epochs)
docker compose run --rm train python train_yolov8n.py \
    --data data.yaml \
    --epochs 10 \
    --batch 16 \
    --img 320 \
    --name quick_test
```

**Output:** `runs/my_first_model/weights/best.pt`

### 4. Export to ONNX (1 minute)

```bash
docker compose run --rm train python export_to_onnx.py \
    /runs/my_first_model/weights/best.pt \
    --output /models \
    --img 320
```

**Output:** `models/best_320.onnx`

### 5. Convert to K230D Format (2-5 minutes)

```bash
docker compose run --rm convert python convert_to_kmodel.py \
    /models/best_320.onnx \
    --output /output \
    --calib-data /datasets/your_dataset/images/val \
    --img 320
```

**Output:** `output/best_320_k230d.kmodel`

### 6. Generate Deployment Script (< 1 minute)

```bash
docker compose run --rm convert python deploy_canmv.py \
    /output/best_320_k230d.kmodel \
    --output /output \
    --img 320 \
    --classes class1 class2  # Your class names
```

**Output:** `output/deploy_k230d.py`

### 7. Deploy to K230D (2 minutes)

```bash
# Copy files to K230D
scp output/best_320_k230d.kmodel root@your-k230d-ip:/root/
scp output/deploy_k230d.py root@your-k230d-ip:/root/

# SSH and run
ssh root@your-k230d-ip
cd /root
python3 deploy_k230d.py
```

## 🎉 Done!

You should now see real-time object detection running on your K230D!

## Using Makefile (Even Easier!)

```bash
# Setup
make setup

# Build
make build

# Train interactively
make train
# Inside container: python train_yolov8n.py --data data.yaml ...

# Or run full pipeline automatically
make full-pipeline
```

## Troubleshooting

**Training out of memory?**
```bash
# Reduce batch size
--batch 8
```

**nncase error during conversion?**
```bash
# Rebuild conversion container
make build-convert
```

**K230D out of memory?**
- Ensure model < 5MB
- Edit `deploy_k230d.py` to increase GC frequency

## Next Steps

- Read full [README.md](README.md) for advanced options
- Optimize model parameters for your use case
- Experiment with different confidence/IoU thresholds
- Profile performance on K230D

## Sample Dataset Structure

```
datasets/
└── my_project/
    ├── images/
    │   ├── train/  (100-10000+ images)
    │   └── val/    (20-1000+ images)
    └── labels/
        ├── train/  (.txt files, one per image)
        └── val/
```

Each label file (e.g., `img1.txt`):
```
0 0.5 0.5 0.3 0.2  # class x_center y_center width height (normalized 0-1)
1 0.7 0.3 0.1 0.15
```

## Performance Expectations

| Stage | Time | Output Size |
|-------|------|-------------|
| Training (100 epochs) | 1-2 hours | ~6 MB (.pt) |
| ONNX Export | 1 min | ~6 MB (.onnx) |
| kmodel Conversion | 2-5 min | ~2-3 MB (.kmodel) |
| K230D Inference | 30-100ms/frame | 10-30 FPS |

---

**Happy detecting! 🎯**

# Project Structure - K230D YOLOv8n Pipeline

Complete overview of the project organization and file purposes.

## Directory Tree

```
k230dev-yolo/
│
├── docker/                           # Docker configurations
│   ├── train/
│   │   └── Dockerfile               # Training environment (PyTorch + Ultralytics)
│   └── convert/
│       └── Dockerfile               # Conversion environment (nncase + .NET)
│
├── workspace/                        # Main working directory (mounted in containers)
│   ├── train_yolov8n.py            # [SCRIPT] Train YOLOv8n model
│   ├── export_to_onnx.py           # [SCRIPT] Export .pt to .onnx
│   ├── convert_to_kmodel.py        # [SCRIPT] Convert .onnx to .kmodel
│   ├── deploy_canmv.py             # [SCRIPT] Generate CanMV deployment script
│   ├── validate_dataset.py         # [UTILITY] Validate dataset structure
│   ├── test_inference.py           # [UTILITY] Test model inference
│   ├── data.yaml.example           # [CONFIG] Dataset configuration template
│   ├── requirements-train.txt      # [CONFIG] Training dependencies
│   └── requirements-convert.txt    # [CONFIG] Conversion dependencies
│
├── datasets/                         # Your training datasets (gitignored)
│   └── your_dataset/
│       ├── images/
│       │   ├── train/              # Training images
│       │   └── val/                # Validation images
│       └── labels/
│           ├── train/              # Training labels (.txt)
│           └── val/                # Validation labels (.txt)
│
├── models/                           # Exported models (gitignored)
│   ├── *.pt                        # PyTorch models
│   └── *.onnx                      # ONNX models
│
├── output/                           # Final outputs (gitignored)
│   ├── *.kmodel                    # K230D models
│   └── deploy_k230d.py             # Generated deployment script
│
├── runs/                             # Training runs (gitignored)
│   └── experiment_name/
│       ├── weights/
│       │   ├── best.pt            # Best model checkpoint
│       │   └── last.pt            # Last model checkpoint
│       ├── results.png            # Training metrics plot
│       └── ...
│
├── test_images/                      # Test images for validation (gitignored)
│
├── docker-compose.yml               # [CONFIG] Docker orchestration
├── Makefile                         # [UTILITY] Command shortcuts
├── start.sh                         # [UTILITY] Interactive startup script
│
├── README.md                        # [DOCS] Main documentation
├── QUICKSTART.md                    # [DOCS] Quick start guide
├── CONTRIBUTING.md                  # [DOCS] Contribution guidelines
├── PROJECT_STRUCTURE.md             # [DOCS] This file
│
├── .gitignore                       # Git ignore rules
├── .dockerignore                    # Docker ignore rules
└── env.example                      # Environment variables template
```

## File Purposes

### Docker Files

| File | Purpose |
|------|---------|
| `docker/train/Dockerfile` | Training environment with PyTorch, Ultralytics, ONNX tools |
| `docker/convert/Dockerfile` | Conversion environment with nncase, .NET SDK |
| `docker-compose.yml` | Orchestrates both services with volume mounts |

### Workspace Scripts

| Script | Purpose | Container |
|--------|---------|-----------|
| `train_yolov8n.py` | Train YOLOv8n at 320×320 | train |
| `export_to_onnx.py` | Export PyTorch → ONNX (Opset 11/12) | train |
| `convert_to_kmodel.py` | Convert ONNX → .kmodel (INT8) | convert |
| `deploy_canmv.py` | Generate CanMV MicroPython script | convert |
| `validate_dataset.py` | Validate YOLO dataset format | train |
| `test_inference.py` | Test model inference on images | train/convert |

### Configuration Files

| File | Purpose |
|------|---------|
| `workspace/data.yaml` | Dataset paths and class definitions |
| `workspace/requirements-train.txt` | Python deps for training |
| `workspace/requirements-convert.txt` | Python deps for conversion |
| `env.example` | Environment variables template |

### Documentation

| File | Purpose |
|------|---------|
| `README.md` | Complete project documentation |
| `QUICKSTART.md` | 5-minute getting started guide |
| `CONTRIBUTING.md` | Contribution guidelines |
| `PROJECT_STRUCTURE.md` | This file - project organization |

### Utility Files

| File | Purpose |
|------|---------|
| `Makefile` | Command shortcuts (make build, make train, etc.) |
| `start.sh` | Interactive menu for common tasks |
| `.gitignore` | Git ignore patterns |
| `.dockerignore` | Docker ignore patterns |

## Data Flow

```
1. Dataset Preparation
   datasets/ → data.yaml

2. Training
   data.yaml → train_yolov8n.py → runs/*/weights/best.pt

3. Export
   best.pt → export_to_onnx.py → models/*.onnx

4. Conversion
   *.onnx → convert_to_kmodel.py → output/*.kmodel

5. Deployment Script Generation
   *.kmodel → deploy_canmv.py → output/deploy_k230d.py

6. K230D Deployment
   *.kmodel + deploy_k230d.py → K230D device
```

## Volume Mounts

Both Docker services mount the following directories:

| Host Directory | Container Path | Purpose |
|----------------|----------------|---------|
| `./workspace` | `/workspace` | Scripts and configs |
| `./datasets` | `/datasets` | Training data |
| `./models` | `/models` | Model files (.pt, .onnx) |
| `./runs` | `/runs` | Training outputs |
| `./output` | `/output` | Final .kmodel files |
| `./test_images` | `/test_images` | Test images |

## Typical Workflow Paths

### Full Development Cycle

```bash
# 1. Setup
make setup
# Edit: workspace/data.yaml

# 2. Validate
make train
python validate_dataset.py --data data.yaml
exit

# 3. Train
make train
python train_yolov8n.py --data data.yaml --epochs 100 --name my_model
exit

# 4. Export
make train
python export_to_onnx.py /runs/my_model/weights/best.pt --output /models
exit

# 5. Convert
make convert
python convert_to_kmodel.py /models/best_320.onnx \
    --calib-data /datasets/my_dataset/images/val
exit

# 6. Deploy Script
make convert
python deploy_canmv.py /output/best_320_k230d.kmodel \
    --classes class1 class2
exit

# 7. Transfer to K230D
scp output/best_320_k230d.kmodel root@k230d:/root/
scp output/deploy_k230d.py root@k230d:/root/
```

### Quick Testing Cycle

```bash
# Automated full pipeline
make full-pipeline

# Results in:
# - output/best_320_k230d.kmodel
# - output/deploy_k230d.py
```

## Container Environments

### Training Container (`train`)
- **Base**: PyTorch 2.1.0 with CUDA 12.1
- **GPU**: Required for training
- **Key Libraries**: ultralytics, onnx, onnxsim
- **Purpose**: Train and export models

### Conversion Container (`convert`)
- **Base**: Ubuntu 22.04
- **GPU**: Not required
- **Key Libraries**: nncase 2.10.0, .NET SDK 8.0
- **Purpose**: Convert to K230D format

## File Size Expectations

| File Type | Typical Size | Notes |
|-----------|--------------|-------|
| `.pt` (PyTorch) | 5-7 MB | YOLOv8n trained model |
| `.onnx` | 5-7 MB | Exported ONNX model |
| `.kmodel` | 2-3 MB | INT8 quantized for K230D |
| Training run | 50-200 MB | Includes checkpoints, plots |
| Dataset | Varies | Depends on image count/size |

## Resource Requirements

### Training
- **GPU**: NVIDIA with 4GB+ VRAM
- **RAM**: 8GB+ system RAM
- **Disk**: 10GB+ free space
- **Time**: 30min - 2hrs (100 epochs)

### Conversion
- **GPU**: Not required
- **RAM**: 4GB+ system RAM
- **Disk**: 5GB+ free space
- **Time**: 2-5 minutes

### K230D Deployment
- **RAM**: Uses ~30-50MB of 128MB
- **Storage**: ~3-5MB for model
- **Inference**: 30-100ms per frame (10-30 FPS)

## Important Notes

1. **Git Ignores**: Large files (datasets, models, runs) are gitignored
2. **Volume Mounts**: All work is persisted through Docker volumes
3. **Container Data**: Containers are stateless; data lives in mounted volumes
4. **Platform**: Developed for Linux/Mac; Windows users should use WSL2

## Getting Help

- **General usage**: See `README.md`
- **Quick start**: See `QUICKSTART.md`
- **Commands**: Run `make help`
- **Interactive**: Run `./start.sh`
- **Script help**: Run any Python script with `--help`

---

**Last Updated**: 2026-01-10

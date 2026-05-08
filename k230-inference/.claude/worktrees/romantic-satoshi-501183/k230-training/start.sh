#!/bin/bash
# Quick start script for K230D YOLOv8n Pipeline

set -e

echo "======================================================================"
echo "  K230D YOLOv8n Object Detection Pipeline"
echo "======================================================================"
echo ""

# Check if Docker is installed
if ! command -v docker &> /dev/null; then
    echo "✗ Docker not found. Please install Docker first."
    echo "  Visit: https://docs.docker.com/get-docker/"
    exit 1
fi

# Check if Docker Compose is installed
if ! docker compose version &> /dev/null; then
    echo "✗ Docker Compose (v2) not found."
    echo "  Please install Docker Compose plugin."
    echo "  Visit: https://docs.docker.com/compose/install/"
    exit 1
fi

echo "✓ Docker found: $(docker --version)"
echo "✓ Docker Compose found: $(docker compose version --short)"
echo ""

# Create directories
echo "Setting up project directories..."
mkdir -p datasets models output runs test_images
echo "✓ Directories created"
echo ""

# Copy example config if needed
if [ ! -f workspace/data.yaml ]; then
    cp workspace/data.yaml.example workspace/data.yaml
    echo "✓ Created workspace/data.yaml from example"
    echo "  ⚠ Please edit workspace/data.yaml with your dataset configuration"
    echo ""
fi

# Check for NVIDIA Docker
echo "Checking NVIDIA Docker support..."
if docker run --rm --gpus all nvidia/cuda:12.1.0-base-ubuntu22.04 nvidia-smi &> /dev/null; then
    echo "✓ NVIDIA Docker available (GPU training enabled)"
else
    echo "⚠ NVIDIA Docker not available"
    echo "  Training will require a GPU. Please install NVIDIA Container Toolkit."
    echo "  Visit: https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/install-guide.html"
fi
echo ""

# Menu
echo "======================================================================"
echo "  What would you like to do?"
echo "======================================================================"
echo ""
echo "1. Build Docker images (first time setup)"
echo "2. Start training container (interactive)"
echo "3. Start conversion container (interactive)"
echo "4. Run full pipeline (automated)"
echo "5. Validate dataset"
echo "6. Exit"
echo ""
read -p "Enter choice [1-6]: " choice

case $choice in
    1)
        echo ""
        echo "Building Docker images..."
        docker compose build
        echo ""
        echo "✓ Build complete!"
        echo "  Next: Run this script again and choose option 2 to start training"
        ;;
    2)
        echo ""
        echo "Starting training container..."
        echo "Tip: Inside container, run:"
        echo "  python train_yolov8n.py --data data.yaml --epochs 100"
        echo ""
        docker compose run --rm train
        ;;
    3)
        echo ""
        echo "Starting conversion container..."
        echo "Tip: Inside container, run:"
        echo "  python convert_to_kmodel.py /models/your_model.onnx"
        echo ""
        docker compose run --rm convert
        ;;
    4)
        echo ""
        if [ ! -f workspace/data.yaml ]; then
            echo "✗ Error: workspace/data.yaml not found"
            echo "  Please configure your dataset first"
            exit 1
        fi
        echo "Running full automated pipeline..."
        echo "This will: train → export → convert → generate deployment script"
        echo ""
        read -p "Continue? [y/N]: " confirm
        if [ "$confirm" = "y" ] || [ "$confirm" = "Y" ]; then
            make full-pipeline
        else
            echo "Cancelled."
        fi
        ;;
    5)
        echo ""
        echo "Validating dataset..."
        docker compose run --rm train python validate_dataset.py --data data.yaml
        ;;
    6)
        echo "Goodbye!"
        exit 0
        ;;
    *)
        echo "Invalid choice"
        exit 1
        ;;
esac

echo ""
echo "======================================================================"
echo "  For more information, see:"
echo "    - README.md (full documentation)"
echo "    - QUICKSTART.md (quick guide)"
echo "    - Makefile (all available commands)"
echo "======================================================================"

# Makefile for K230D YOLOv8n Pipeline
# Simplifies common development tasks

.PHONY: help build build-train build-convert up-train up-convert down clean setup train export convert deploy test

# Default target
help:
	@echo "K230D YOLOv8n Development Pipeline"
	@echo ""
	@echo "Setup Commands:"
	@echo "  make setup          - Create directories and prepare environment"
	@echo "  make build          - Build all Docker images"
	@echo "  make build-train    - Build training image only"
	@echo "  make build-convert  - Build conversion image only"
	@echo ""
	@echo "Development Commands:"
	@echo "  make train          - Start training container (interactive)"
	@echo "  make convert        - Start conversion container (interactive)"
	@echo "  make down           - Stop all containers"
	@echo ""
	@echo "Workflow Commands:"
	@echo "  make full-pipeline  - Run complete training → export → convert workflow"
	@echo ""
	@echo "Utility Commands:"
	@echo "  make clean          - Remove generated files (keeps models)"
	@echo "  make clean-all      - Remove all generated files including models"
	@echo "  make test           - Run basic tests"
	@echo ""

# Setup project directories
setup:
	@echo "Setting up project directories..."
	mkdir -p datasets models output runs test_images
	@if [ ! -f workspace/data.yaml ]; then \
		cp workspace/data.yaml.example workspace/data.yaml; \
		echo "✓ Created workspace/data.yaml from example"; \
		echo "⚠ Please edit workspace/data.yaml with your dataset configuration"; \
	else \
		echo "✓ workspace/data.yaml already exists"; \
	fi
	@echo "✓ Setup complete"

# Build all Docker images
build:
	@echo "Building all Docker images..."
	docker compose build
	@echo "✓ Build complete"

# Build training image
build-train:
	@echo "Building training image..."
	docker compose build train
	@echo "✓ Training image built"

# Build conversion image
build-convert:
	@echo "Building conversion image..."
	docker compose build convert
	@echo "✓ Conversion image built"

# Start training container (interactive)
train:
	@echo "Starting training container..."
	@echo "Tip: Use 'python train_yolov8n.py --help' for options"
	docker compose run --rm train

# Start conversion container (interactive)
convert:
	@echo "Starting conversion container..."
	@echo "Tip: Use 'python convert_to_kmodel.py --help' for options"
	docker compose run --rm convert

# Stop all containers
down:
	@echo "Stopping all containers..."
	docker compose down
	@echo "✓ Containers stopped"

# Quick training example (requires data.yaml to be configured)
train-example:
	@echo "Running example training (100 epochs, 320x320)..."
	@if [ ! -f workspace/data.yaml ]; then \
		echo "✗ Error: workspace/data.yaml not found"; \
		echo "  Run 'make setup' first and configure your dataset"; \
		exit 1; \
	fi
	docker compose run --rm train python train_yolov8n.py \
		--data data.yaml \
		--epochs 100 \
		--batch 16 \
		--img 320 \
		--name example_model

# Export trained model to ONNX
export-model:
	@echo "Exporting model to ONNX..."
	@if [ -z "$(MODEL)" ]; then \
		echo "✗ Error: MODEL not specified"; \
		echo "  Usage: make export-model MODEL=/runs/my_model/weights/best.pt"; \
		exit 1; \
	fi
	docker compose run --rm train python export_to_onnx.py \
		$(MODEL) \
		--output /models \
		--img 320 \
		--opset 11

# Convert ONNX to kmodel
convert-model:
	@echo "Converting ONNX to kmodel..."
	@if [ -z "$(ONNX)" ]; then \
		echo "✗ Error: ONNX not specified"; \
		echo "  Usage: make convert-model ONNX=/models/best_320.onnx"; \
		exit 1; \
	fi
	docker compose run --rm convert python convert_to_kmodel.py \
		$(ONNX) \
		--output /output \
		--img 320

# Generate deployment script
deploy-script:
	@echo "Generating deployment script..."
	@if [ -z "$(KMODEL)" ]; then \
		echo "✗ Error: KMODEL not specified"; \
		echo "  Usage: make deploy-script KMODEL=/output/best_320_k230d.kmodel"; \
		exit 1; \
	fi
	docker compose run --rm convert python deploy_canmv.py \
		$(KMODEL) \
		--output /output \
		--img 320

# Run full pipeline (requires configured data.yaml)
full-pipeline:
	@echo "=" 
	@echo "Running full K230D YOLOv8n pipeline"
	@echo "="
	@if [ ! -f workspace/data.yaml ]; then \
		echo "✗ Error: workspace/data.yaml not found"; \
		echo "  Run 'make setup' first and configure your dataset"; \
		exit 1; \
	fi
	@echo ""
	@echo "Step 1/4: Training YOLOv8n..."
	docker compose run --rm train python train_yolov8n.py \
		--data data.yaml \
		--epochs 50 \
		--batch 16 \
		--img 320 \
		--name pipeline_model
	@echo ""
	@echo "Step 2/4: Exporting to ONNX..."
	docker compose run --rm train python export_to_onnx.py \
		/runs/pipeline_model/weights/best.pt \
		--output /models \
		--img 320
	@echo ""
	@echo "Step 3/4: Converting to kmodel..."
	docker compose run --rm convert python convert_to_kmodel.py \
		/models/best_320.onnx \
		--output /output \
		--img 320
	@echo ""
	@echo "Step 4/4: Generating deployment script..."
	docker compose run --rm convert python deploy_canmv.py \
		/output/best_320_k230d.kmodel \
		--output /output \
		--img 320
	@echo ""
	@echo "✓ Pipeline complete!"
	@echo "  Model: output/best_320_k230d.kmodel"
	@echo "  Script: output/deploy_k230d.py"

# Clean generated files (keep models)
clean:
	@echo "Cleaning generated files..."
	rm -rf runs/*/
	rm -rf output/*.kmodel output/*.py
	@echo "✓ Clean complete (models preserved)"

# Clean everything including models
clean-all:
	@echo "Cleaning all generated files..."
	rm -rf runs/* models/* output/*
	@echo "✓ All generated files removed"

# Run basic tests
test:
	@echo "Running basic tests..."
	@echo "Checking Docker installation..."
	@docker --version || (echo "✗ Docker not found" && exit 1)
	@echo "✓ Docker OK"
	@echo "Checking Docker Compose..."
	@docker compose version || (echo "✗ Docker Compose not found" && exit 1)
	@echo "✓ Docker Compose OK"
	@echo "Checking NVIDIA Docker..."
	@docker run --rm --gpus all nvidia/cuda:12.1.0-base-ubuntu22.04 nvidia-smi > /dev/null 2>&1 || \
		(echo "⚠ NVIDIA Docker not available (required for training)" && exit 1)
	@echo "✓ NVIDIA Docker OK"
	@echo "Checking project structure..."
	@test -f docker-compose.yml || (echo "✗ docker-compose.yml not found" && exit 1)
	@test -d workspace || (echo "✗ workspace/ not found" && exit 1)
	@echo "✓ Project structure OK"
	@echo ""
	@echo "All tests passed! ✓"

# Show container logs
logs-train:
	docker compose logs train

logs-convert:
	docker compose logs convert

# Interactive shell in containers
shell-train:
	docker compose run --rm train /bin/bash

shell-convert:
	docker compose run --rm convert /bin/bash

# Check versions
versions:
	@echo "Installed Versions:"
	@echo "Docker: $$(docker --version)"
	@echo "Docker Compose: $$(docker compose version)"
	@echo ""
	@echo "Training Container:"
	@docker compose run --rm train python -c "import torch; print(f'PyTorch: {torch.__version__}')" 2>/dev/null || echo "Not built yet"
	@docker compose run --rm train python -c "import ultralytics; print(f'Ultralytics: {ultralytics.__version__}')" 2>/dev/null || echo "Not built yet"
	@echo ""
	@echo "Conversion Container:"
	@docker compose run --rm convert python3 -c "import nncase; print(f'nncase: {nncase.__version__}')" 2>/dev/null || echo "Not built yet"

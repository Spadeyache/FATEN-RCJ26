# Training Environment for YOLOv8n
FROM pytorch/pytorch:2.1.0-cuda12.1-cudnn8-runtime

# Set working directory
WORKDIR /workspace

# Prevent interactive prompts during build
ENV DEBIAN_FRONTEND=noninteractive

# Install system dependencies
RUN apt-get update && apt-get install -y \
    git \
    wget \
    curl \
    vim \
    libgl1-mesa-glx \
    libglib2.0-0 \
    libsm6 \
    libxext6 \
    libxrender-dev \
    libgomp1 \
    && rm -rf /var/lib/apt/lists/*

# Install Python packages for training
RUN pip install --no-cache-dir \
    ultralytics==8.1.0 \
    onnx==1.15.0 \
    onnxsim==0.4.35 \
    onnxruntime-gpu==1.16.3 \
    opencv-python==4.8.1.78 \
    numpy==1.24.3 \
    pillow==10.1.0 \
    pyyaml==6.0.1 \
    tqdm==4.66.1 \
    matplotlib==3.8.2 \
    seaborn==0.13.0 \
    tensorboard==2.15.1

# Create necessary directories
RUN mkdir -p /datasets /models /runs /workspace

# Set environment variables
ENV PYTHONUNBUFFERED=1
ENV YOLO_CONFIG_DIR=/workspace/.config

CMD ["/bin/bash"]

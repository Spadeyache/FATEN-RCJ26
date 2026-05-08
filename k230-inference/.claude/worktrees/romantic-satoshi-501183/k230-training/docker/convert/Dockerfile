# ベースイメージはソース推奨のUbuntu 20.04を使用
FROM ubuntu:20.04

# 環境変数の設定（対話型プロンプトの停止）
ENV DEBIAN_FRONTEND=noninteractive

# 1. 必要最小限のシステムパッケージのインストール
RUN apt-get update && apt-get install -y \
    wget \
    curl \
    python3 \
    python3-pip \
    libglib2.0-0 \
    libstdc++6 \
    libxcb1 \
    && rm -rf /var/lib/apt/lists/*

# .NET 7.0 Runtimeのインストール (nncase v2に必須)
RUN wget https://dot.net/v1/dotnet-install.sh -O dotnet-install.sh \
    && chmod +x dotnet-install.sh \
    && ./dotnet-install.sh --channel 7.0 --runtime dotnet \
    && rm dotnet-install.sh

# DOTNET_ROOTを正しいパスに設定（重要）
ENV DOTNET_ROOT=/root/.dotnet
ENV PATH=$PATH:/root/.dotnet

# 2. Pythonライブラリのインストール
# Pin protobuf<4: onnx 1.9 + generated _pb2 are incompatible with protobuf 4/5
# (TypeError: Descriptors cannot be created directly)
RUN pip3 install --no-cache-dir \
    "protobuf>=3.20.1,<4" \
    nncase==2.9.0 \
    nncase-kpu==2.9.0 \
    numpy==1.19.5 \
    onnx==1.9.0 \
    onnx-simplifier==0.3.6 \
    onnxruntime==1.8.0 \
    opencv-python-headless \
    pillow

# 3. libstdc++のリンク（GLIBCXX対策）
RUN find /root/.dotnet/shared/Microsoft.NETCore.App/ -maxdepth 1 -mindepth 1 -type d \
    -exec ln -sf /usr/lib/x86_64-linux-gnu/libstdc++.so.6 {}/libstdc++.so.6 \;

# 作業ディレクトリの設定
WORKDIR /workspace
RUN mkdir -p /workspace/models /workspace/output /workspace/calibration_data

# インストール確認
RUN python3 -c "import nncase; print('nncase OK')" && \
    python3 -c "import nncase_kpu; print('nncase_kpu OK')" && \
    python3 -c "import cv2; print('cv2 OK')"

CMD ["/bin/bash"]
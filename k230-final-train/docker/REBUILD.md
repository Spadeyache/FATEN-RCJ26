# Rebuilding the Docker images

`docker-compose.yml` uses three prebuilt images. They already exist on the dev
machine, but if you move to a new PC (or delete an image), rebuild them from the
Dockerfiles preserved here — this folder is now fully self-contained.

```powershell
# from k230-final-train/
docker build -t k230d-train:latest   docker/train      # GPU PyTorch + ultralytics 8.1.0
docker build -t k230-nncase:latest   docker/nncase     # nncase 2.11 + nncase-kpu + JupyterLab
docker build -t k230d-convert:latest docker/convert    # nncase 2.9.0 + .NET (alt, pinned)
```

Notes:
- `k230-nncase` (docker/nncase) installs the latest nncase -> **2.11.0**, which is
  the verified-best for this YOLOv8n (uint8 kmodel ≈ float ONNX). This is the
  image the pipeline/notebook use for convert + simulate.
- `k230d-convert` (docker/convert) is pinned to **nncase 2.9.0** — kept only as a
  reference/fallback. 2.9.0 showed the uint8 detection-head quant damage, so
  prefer 2.11 unless your K230D firmware requires a 2.9 kmodel.
- `k230d-train` (docker/train) needs an NVIDIA GPU + nvidia-container-toolkit.

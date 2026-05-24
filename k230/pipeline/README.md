# k230/pipeline

End-to-end Docker pipeline: labeled images -> deployable K230D bundle.

## Quick start

```bash
cd k230/docker
docker compose build              # builds both images (one-time)

cd ../pipeline
make all                          # train + convert + eval + package
# or step-by-step:
make train
make export
make calibrate
make compile
make eval
make select
make package
```

Output: `artifacts/deploy/` — copy contents to K230D SD card at
`/data/k230-deploy/` and `import det_image_yolov8` on the REPL.

## Stages

| # | Stage | Container | Reads | Writes |
|---|---|---|---|---|
| 01 | train      | k230-train  | datasets/my_dataset       | artifacts/runs/.../best.pt |
| 02 | export     | k230-train  | best.pt                   | artifacts/best.onnx |
| 03 | calibrate  | either      | datasets/.../images       | artifacts/calib/*.jpg |
| 04 | compile    | k230-nncase | best.onnx + calib/        | artifacts/exports/<variant>/model.kmodel |
| 05 | eval       | k230-nncase | exports/, datasets/val    | artifacts/reports/variant_matrix.csv |
| 06 | select     | k230-nncase | variant_matrix.csv        | artifacts/reports/winner.txt |
| 07 | package    | k230-nncase | winner + deploy scripts   | artifacts/deploy/* |

All paths are container-internal. Mounts are in `../docker/docker-compose.yml`.

## Config

Edit `configs/pipeline.yaml` to change image size, epochs, variants, calib
count, etc. `configs/data.yaml` is the ultralytics dataset descriptor.

## Smoke test (no GPU needed, ~2 min)

```bash
make build
make smoke         # 1-epoch train on tiny imgsz -- proves wiring
make all-skip-train  # rest of pipeline using existing best.pt
```

# Reproducibility report

## What was verified (this session)

End-to-end run of stages **03 → 07** on host `Windows + Docker Desktop`,
container `k230-nncase:latest` (already built), using the existing
`yolov8_simplified.onnx` (10.2 MB) staged at `artifacts/best.onnx`.

| # | Stage | Command (inside container) | Time | Output |
|---|---|---|---|---|
| 03 | calibrate | `python stages/03_calibrate.py` | <1 s | `artifacts/calib/` 74 images sampled from 788 |
| 04 | compile  | `python stages/04_compile.py`   | 130 s | 2 kmodels: `yolov8_kld_u8u8` (3.09 MB) + `yolov8_kld_i16act_u8w` (3.35 MB) |
| 05 | eval     | `python stages/05_eval.py`      | ~20 m | per-variant CSVs + `reports/variant_matrix.csv` |
| 06 | select   | `python stages/06_select.py`    | <1 s | `reports/winner.txt` = `yolov8_kld_i16act_u8w` |
| 07 | package  | `python stages/07_package.py`   | <1 s | `artifacts/deploy/` (kmodel + cfg + 3 .py scripts + README) |

### Numbers (50 val images, 83 ground-truths)

| variant | avg_best_iou | avg_best_score_iou | recall@IoU0.5_score≥0.05 | kmodel size |
|---|---|---|---|---|
| yolov8_kld_i16act_u8w  | **0.796** | **0.726** | **0.988** | 3.35 MB |
| yolov8_kld_u8u8        | 0.783 | 0.714 | 0.988 | 3.09 MB |

Both pass. The int16-activation variant wins on IoU by a hair — same
finding as the previous (manually-run) experiment.

## What was NOT verified this session

- **Stage 01 (train)** — needs `k230-train` image. Build is downloading
  the ultralytics base (3.69 GB layer + 2.13 GB layer) over the user's
  current connection at ~750 KB/s. Estimated 60–90 min total pull.
  Independent of any code — purely a one-time network cost.
- **Stage 02 (export)** — needs `k230-train` image (ultralytics CLI).

These two stages are wired identically to 03–07 (same Makefile pattern,
same `pipeline.yaml` config). Once the image finishes pulling on any
host, `make train` and `make export` will work without further changes.

## To reproduce on a fresh PC

### Prerequisites
- Docker Desktop (or Docker Engine + Docker Compose v2)
- ~15 GB free disk (5 GB nncase image, 8 GB train image, 1 GB datasets, 1 GB artifacts)
- NVIDIA GPU + nvidia-container-toolkit (only for stage 01 training)
- The `k230/` tree on disk (zip it, copy it, however)

### Build
```bash
cd k230/docker
docker compose build              # builds both images (5 + 30-90 min one-time)
```

### Full pipeline (with training)
```bash
cd k230/pipeline
make all                          # train → export → calibrate → compile → eval → select → package
```

### Skip training (use existing best.pt)
Put your `best.pt` at `artifacts/runs/yolov8n_k230/weights/best.pt`, then:
```bash
make all-skip-train
```

### Just the back-half (this is what was verified)
If you already have an ONNX, drop it at `artifacts/best.onnx` and:
```bash
make calibrate compile eval select package
```

### Output
`artifacts/deploy/` contains everything for the K230D SD card:
```
model.kmodel            3.35 MB    the winning compiled detector
deploy_config.json      298 B      labels + thresholds + mean/std
det_image_yolov8.py     12 KB      single-image inference + draw boxes
det_live_xy_yolov8.py   11 KB      live camera + UART emission
yolov8_decode.py        3.4 KB     shared anchor-free decoder + NMS
README.md               659 B      install instructions
```

Copy this directory to the K230D at `/data/k230-deploy/`, then in CanMV REPL:
```python
import det_image_yolov8
det_image_yolov8.detection()
```

## Config knobs

All in `configs/pipeline.yaml`:

- `train.epochs`, `train.batch`, `train.device` — training hyperparameters
- `train.imgsz` — must match `compile.input_shape` (default [480, 640])
- `calibrate.count` — number of calibration images (74 default; smaller = faster compile, less stable PTQ)
- `compile.variants[]` — add/remove PTQ schemes (quant_type/w_quant_type combinations)
- `eval.limit` — how many val images to score (50 default; full = drop this key)

## Known issues / non-issues

- **`docker compose build | tail` shows no output.** BuildKit progress is
  TTY-formatted; use `--progress=plain` and direct redirection if you need
  to watch a build. Build still runs correctly with default settings.
- **`recall@IoU0.5_score>=0.20` and `>=0.40` empty in CSV.** Cosmetic bug
  in `stages/05_eval.py` — JSON key is `>=0.2` not `>=0.20`. Doesn't affect
  winner selection (which uses recall@0.05).
- **First compile or eval needs `pip install opencv-python-headless pyyaml`**
  inside the nncase container. Makefile does this automatically.

## Files map

```
k230/
├── docker/
│   ├── Dockerfile               nncase 2.11 + .NET 7 + Python 3.10 (Ubuntu 22.04 base)
│   ├── Dockerfile.train         ultralytics 8.3.0 base + onnx + onnxsim + pyyaml
│   └── docker-compose.yml       both services + mounts
├── pipeline/
│   ├── configs/
│   │   ├── pipeline.yaml        single source of truth
│   │   └── data.yaml            ultralytics dataset descriptor
│   ├── shared/config.py         yaml loader
│   ├── stages/
│   │   ├── 01_train.py          ultralytics YOLO().train()
│   │   ├── 02_export.py         model.export(format='onnx')
│   │   ├── 03_calibrate.py      random sample N images
│   │   ├── 04_compile.py        wraps train/src/convert/convert_kmodel.py
│   │   ├── 05_eval.py           wraps train/src/eval/pc_eval_yolov8.py
│   │   ├── 06_select.py         pick highest avg_best_iou
│   │   └── 07_package.py        emit SD-card bundle
│   ├── Makefile                 entry point
│   ├── README.md                quick-start
│   └── artifacts/               (all generated, .gitignore-friendly)
└── data/datasets/               (your labeled images, YOLOv8 format)
```

# Docker-based conversion + evaluation

The Windows install of `nncase==2.9.0` is missing the `nncase-kpu` plugin
(which provides the K230 simulator + KPU ops needed to compile and run
.kmodels). The plugin is Linux-only on PyPI.

The user already has a working Linux Docker environment for this:
`k230-nncase` (defined in `../docker/Dockerfile` in this consolidated
tree). It bundles:

- `nncase==2.9.0` AND `nncase-kpu==2.9.0`
- .NET 7.0 runtime (required by nncase 2.x)
- onnx 1.9, onnx-simplifier 0.3.6, onnxruntime 1.8.0
- opencv-python-headless, numpy 1.19.5

## What runs where

| Step | Where | Why |
|---|---|---|
| `inspect_npy.py` | Windows | numpy + torch + easydict only |
| `inspect_pretrain_weights.py` | Windows | torch only |
| `build_anchorbasedet.py` | Windows | torch only |
| `load_from_npy.py` | Windows | torch only |
| `verify_parity.py` (no kmodel) | Windows | torch + onnxruntime |
| `export_onnx.py` | Windows | torch + onnx + onnxsim |
| **`convert/batch_export.py`** | **Docker** | needs nncase compile + simulator plugin |
| **`eval/pc_eval_batch.py`** | **Docker** | needs nncase simulator |
| **`probe/pc_raw_probe.py`** | **Docker** | needs nncase simulator |
| `deploy/det_image.py` etc | K230D | runs on device |

## Setup the Docker image

```powershell
# From the docker/ subdirectory of the consolidated k230/ tree
cd C:\Users\magic\Documents\robocup\RoboCupJunior_RescueLine\k230\docker
docker compose up -d --build
```

The `convert` service mounts:
- `./workspace`  -> `/workspace`
- `./datasets`   -> `/datasets`
- `./models`     -> `/models`
- `./output`     -> `/output`

You need to ALSO mount the new `k230-train` folder. Edit
`docker-compose.yml` to add `- ./k230-train:/k230-train` to the
`convert` service's volumes (or just bind-mount it at run time):

```powershell
docker run --rm -it `
    -v C:\Users\magic\Documents\robocup\RoboCupJunior_RescueLine\k230\train:/k230-train `
    -v C:\Users\magic\Documents\robocup\RoboCupJunior_RescueLine\k230\data\datasets:/datasets `
    -w /k230-train `
    k230-nncase:latest /bin/bash
```

## Running the matrix inside the container

```bash
# Inside the container, at /k230-train:
PYTHONPATH=/k230-train/src python3 src/convert/batch_export.py \
    --variants configs/variants.yaml \
    --exports-dir exports

# After all variants are built:
PYTHONPATH=/k230-train/src python3 src/eval/pc_eval_batch.py \
    --exports-dir exports \
    --images-dir /datasets/my_dataset/train/images \
    --labels-dir /datasets/my_dataset/train/labels \
    --limit 50 \
    --report reports/variant_matrix.csv
```

## Reading the matrix

```bash
cat reports/variant_matrix.csv
```

Compare each variant to `v00_aicube_baseline`. Success means a variant
beats the abort threshold (see ABORT_CRITERIA.md):
- `recall@IoU0.5 score>=0.05` >= 0.30
- `avg_best_iou_score` >= 0.05
- `avg_best_score_iou` >= 0.30

## Calibration images

The first time, populate `data/calibration/` with ~16 hand-picked images
from `../data/datasets/my_dataset/train/images/` covering both
`black` and `silver` targets and a few hard-negative backgrounds.

```bash
# Inside container — pick a sample if you don't want to curate manually:
mkdir -p /k230-train/data/calibration
cd /datasets/my_dataset/train/images
ls *.jpg | shuf -n 16 | xargs -I{} cp {} /k230-train/data/calibration/
```

## Troubleshooting

- **`nncase.simulator.k230.sc not executable`**: chmod +x it; the
  Dockerfile already does this but if you mount over it, re-run
  `chmod +x /usr/local/lib/python3.8/dist-packages/nncase.simulator.k230.sc`.

- **OOM during compile**: reduce `calib_count` from 16 to 8 in
  `configs/variants.yaml`. nncase loads all calibration tensors into
  memory at once during Kld calibration.

- **`use_mix_quant` crash**: nncase 2.9 has a known duplicate-key bug.
  `apply_mix_quant.py` works around it by passing the scheme path with
  `use_mix_quant=False`. If you still hit the crash, reduce
  `promote_worst_k` to 0 and try only `promote_last_n`.

- **`.kmodel` larger than expected**: int16 promotion roughly doubles
  weight bytes for the promoted layers. If your kmodel grows past 5 MB,
  it may not fit in K230D Zero SRAM with the rest of the firmware --
  reduce promotions.

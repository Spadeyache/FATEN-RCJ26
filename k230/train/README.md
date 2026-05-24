# k230-train

A reliable nncase 2.9.0 conversion + evaluation + deployment pipeline for the
K230D Zero (CanMV v1.5-legacy) AnchorBaseDet detector trained in AI Cube.

This folder is a fresh workspace. Datasets live next door at
`..\data\datasets\` (copied from the original `k230-training\datasets\`).
The `..\..\k230-training\` and `..\..\k230-inference\` repos are
referenced but not modified.

## Why this exists

The AI Cube `.kmodel` looks good in the AI Cube preview but performs poorly
when tested as the final deployed model:

- avg best_iou around 0.67-0.70 (geometry is OK)
- avg score of the best-IoU candidate near zero (~0.0008)
- avg IoU of the best-score candidate around 0.02
- recall@IoU0.5 score>=0.05 around 0.04-0.06

Classic signature of **detection-head activation quantization damage**. The
plan tests three concrete fixes (activation int16, baked-ImageNet
reproduction, MixQuant) on a kmodel we recompile from the original AI Cube
weights.

The full plan is at `..\..\..\.claude\plans\there-are-somany-things-quirky-candy.md`.

## What we discovered (already done; see reports/)

1. `inspect_npy.py` decoded AI Cube's `.npy` ->
   `[model_type, cfg, input_size=640, anchors, mean, std, state_dict(401 keys)]`.
2. `inspect_pretrain_weights.py` + state-dict shape inference revealed the
   architecture: `can3` backbone (MobileNetV3-Small-like, width_mult=0.5) +
   YOLOv5-style FPN/PAN neck + YOLOv5 Detect head (3 anchors x (5+2) at
   strides 8/16/32).
3. `build_anchorbasedet.py` reconstructs the architecture in PyTorch.
   `load_from_npy.py` strict-loads the .npy into it -- 401/401 keys match,
   zero shape mismatches.
4. `export_onnx.py` exports float32 ONNX. ORT parity vs PyTorch is ~1e-4
   on all 3 outputs; output shapes match the documented kmodel I/O
   (`(1,60,80,21)`, `(1,30,40,21)`, `(1,15,20,21)`).

The remaining steps (compile + variants matrix + on-device deploy) need
the Linux Docker container because Windows nncase is missing the KPU
plugin -- see `docs\DOCKER_PIPELINE.md`.

## Folder structure

```
k230-train\
├── configs\        Templates and the variants.yaml matrix
├── src\
│   ├── common\     Shared decoder, letterbox, config, calibration, nncase-compat
│   ├── inspectors\ Read-only inspections (.npy, .pth, .kmodel)
│   ├── reconstruct\ Path A: rebuild AnchorBaseDet PyTorch model from AI Cube .npy
│   ├── convert\    nncase compile entry + diagnose/MixQuant + batch_export
│   ├── eval\       PC evaluator (single + batch) and PC-vs-K230 diff
│   ├── probe\      Single-image raw-tensor probes
│   ├── deploy\     K230D MicroPython scripts (det_image, det_video, raw probe)
│   └── train_yolov5n\ Fallback README (scaffolded only if Path A fails)
├── data\
│   ├── calibration\  16 hand-picked calibration images (already populated)
│   ├── test_images\  Put 5-10 GT-labeled images here for quick sanity checks
│   └── anchorbasedet_reconstructed.onnx   (created by export_onnx.py)
│   └── anchorbasedet_reconstructed.pt     (created by load_from_npy.py)
├── exports\
│   ├── v00_aicube_baseline\          Read-only copy of AI Cube .kmodel
│   ├── v04_raw01_kld_i16act_u8w\     Activation int16 (hypothesis 1)
│   ├── v06_baked_imagenet_kld_u8u8\  Reproduce AI Cube via our pipeline
│   └── v09_raw01_mixquant\           MixQuant (heads -> int16)
├── reports\        npy_inspection, kmodel_io_inspection, parity, variant_matrix
└── docs\           ENV_SETUP, METRICS, ABORT_CRITERIA, DOCKER_PIPELINE
```

## Environment

Two parts:

- **Windows host** -- runs inspection, reconstruction, ONNX export.
  Python 3.10 + torch + onnx + onnxsim + onnxruntime + easydict + opencv.
  See `requirements.txt`.
- **Linux Docker `k230-nncase`** -- runs the nncase compile pipeline
  and the PC simulator evaluations. nncase 2.9.0 + nncase-kpu 2.9.0 +
  .NET 7.0. Defined at `..\docker\Dockerfile` (consolidated tree).
  See `docs\DOCKER_PIPELINE.md`.

```powershell
# Windows side
py -m pip install -r requirements.txt
```

## Execution order

```powershell
# --- Windows (already executed, results in reports/) ---
py src\inspectors\inspect_npy.py "C:\Users\magic\Downloads\k230model\AICube_V1.4_for_Windows\AICube_for_Windows\example_projects\IR\model\best_AnchorBaseDet_can3_5_n_20260514232500.npy" > reports\npy_inspection.txt
py src\inspectors\dump_state_dict.py  "...same .npy..."
py src\reconstruct\load_from_npy.py
py src\reconstruct\export_onnx.py --simplify

# --- Docker container ---
# See docs/DOCKER_PIPELINE.md for the mount + run command.
# Inside the container at /k230-train:
PYTHONPATH=/k230-train/src python3 src/convert/batch_export.py \
    --variants configs/variants.yaml --exports-dir exports

PYTHONPATH=/k230-train/src python3 src/eval/pc_eval_batch.py \
    --exports-dir exports \
    --images-dir /datasets/my_dataset/train/images \
    --labels-dir /datasets/my_dataset/train/labels \
    --limit 50 --report reports/variant_matrix.csv

# --- K230D Zero (after picking a variant) ---
# Copy chosen exports/<best>/model.kmodel + deploy_config.json to /data/k230-train
# on the K230D SD card. Then on the K230D MicroPython REPL:
#     import det_image; det_image.detection()
```

## Metric interpretation

See `docs\METRICS.md`. Quick reference:

| Metric | Healthy | Baseline (broken) |
|---|---|---|
| `recall@IoU0.5 score>=0.2` | >= 0.6 | ~0 |
| `recall@IoU0.5 score>=0.05` | >= 0.8 | 0.04-0.06 |
| `avg_best_iou` | >= 0.7 | 0.67-0.70 (already fine) |
| `avg_best_iou_score` | >= 0.4 | 0.0008 |
| `avg_best_score_iou` | >= 0.5 | 0.02 |

## Known limitations

- AI Cube training source is compiled to .exe (Nuitka); we can't replicate
  its training/augmentation pipeline exactly. We use the trained weights
  via PyTorch reconstruction + clean ONNX export, which is sufficient
  for re-quantization.
- Quantization has some non-determinism; two compiles of the same variant
  can differ by 1-3% in recall.
- On-device verification requires physical K230D access.
- The pipeline assumes ImageNet mean/std baked into the kmodel (as
  evidenced by `example_projects/IR/config.json`). If your variant uses
  `preprocess_mode=raw_01` you need a different `deploy_config.json`
  (mean=0, std=1 there is just metadata; K230D ai2d feeds raw uint8
  regardless).

## Status

| Step | Status |
|---|---|
| `.npy` inspection (decision gate) | DONE -- Path A unlocked |
| Architecture reconstruction | DONE -- 401/401 keys match |
| ONNX export | DONE -- ORT parity 1e-4, float recall@0.4 = 0.986 |
| Convert + eval scripts | DONE |
| Build `k230d-convert` Docker image | DONE |
| Variants matrix run | DONE -- see `reports/variant_matrix.csv` |
| Pick best variant | DONE -- **`v_float32`** |
| K230D deployment scripts | DONE -- runs on device |

## Result -- the winner is `v_float32`

| metric | `v_float32` | AI Cube `v00` baseline | gap |
|---|---|---|---|
| recall@IoU0.5 score>=0.05 | 0.987 | 0.026 | **38x** |
| recall@IoU0.5 score>=0.20 | 0.987 | 0.013 | **76x** |
| recall@IoU0.5 score>=0.40 | 0.987 | 0.000 | infinite |
| avg_best_iou_score | 0.524 | 0.001 | **520x** |
| avg_best_score_iou | 0.532 | 0.022 | **24x** |
| size | 3.98 MB | 1.35 MB | 3x larger |

Five uint8 / int16 PTQ variants were tested (v04, v06, v09, v10 -- see
`reports/variant_matrix.csv`). **None recover usable accuracy.** MixQuant
with 12 layers promoted to int16 hits 0.105 recall@0.05. MixQuant with
126 layers promoted hits 0.066 (worse). The architecture's uint8 PTQ
sensitivity is structural, not localised. See `reports/DIAGNOSIS_RESULT.md`
for the full chain of evidence.

## Deployment

```bash
# 1. From your dev machine: copy these two files to the K230D SD card
# at /data/k230-train/:
#   - exports/v_float32/model.kmodel
#   - exports/v_float32/deploy_config.json
#
# 2. Copy the deployment scripts:
#   - src/deploy/det_image.py        (single-image inference)
#   - src/deploy/det_video.py        (live camera)
#   - src/deploy/k230_raw_probe.py   (PC<->K230 parity check)
#
# 3. Edit ROOT_PATH at top of each .py if /data/k230-train isn't where you
#    put them.
#
# 4. From CanMV REPL:
#       import det_image
#       det_image.detection()
```

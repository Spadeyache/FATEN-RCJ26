# YOLOv5n fallback branch

This directory exists ONLY for the abort path described in
[`../../docs/ABORT_CRITERIA.md`](../../docs/ABORT_CRITERIA.md).

You do NOT need to use this branch unless:

- The reconstructed AnchorBaseDet from `.npy` cannot beat the AI Cube
  baseline on the variant matrix.
- OR the `.npy` cannot be loaded.

## Why YOLOv5n is the pivot

YOLOv5n's Detect head IS AnchorBaseDet-compatible (3 anchors × (5+nc) per
scale, strides 8/16/32). The existing `aicube.anchorbasedet_post_process`
on K230D works as-is — only `deploy_config.json`'s anchors and mean/std
need updates (use `/255` only; no ImageNet normalization, which is what
makes YOLOv5n's heads quantization-robust).

## Status

NOT scaffolded yet. Path A (reconstruction from .npy) succeeded
(401/401 state_dict keys matched, ORT parity within 1e-4). Run the
variant matrix first — only if it fails should you build out this branch.

When you do need it:

1. `prepare_dataset.py` — split `../../../data/datasets/my_dataset` into
   YOLOv5 train/val. Reuse the existing YOLOv5 repo at
   `../../../../k230-training/workspace/yolov5/` (still at its original
   location; not part of the consolidated `k230/` tree).
2. `train.py` — wraps the YOLOv5 trainer with anchors from
   `../../exports/v00_aicube_baseline/deploy_config.json` (the same anchors
   AI Cube auto-derived for this dataset).
3. `export_onnx.py` — `.pt` → `.onnx` at static `[1, 3, 480, 640]`.
4. Feed the new ONNX through the existing `convert/batch_export.py`
   pipeline. Don't change anything else — same variants, same eval.

Estimated wall clock: 6-12 h training, 1 h conversion + eval.

## Key training settings (for when you scaffold this)

- Image size: 640x480 (matches deployment).
- Anchors: from `deploy_config.json`:
  - stride 8: 33,43, 41,56, 55,63
  - stride 16: 59,81, 72,92, 87,111
  - stride 32: 103,134, 161,121, 134,157
- Hyperparameters: start from YOLOv5n defaults; disable mosaic (small
  dataset benefits more from real augmentation) but keep flip/hue/sat.
- Set `--noautoanchor` to keep the AI Cube anchors fixed.
- Use BCEWithLogits loss (matches AnchorBaseDet's sigmoid-in-head).
- Train ~100 epochs; watch val mAP curve plateau.

## Why we don't use ImageNet mean/std here

AnchorBaseDet was trained with ImageNet normalization in the AI Cube
pipeline. The hypothesis behind this fallback is exactly that ImageNet
normalization is what's making the detection-head quantization fragile.
YOLOv5n trained without ImageNet normalization should be more robust to
uint8 PTQ. Use `preprocess_mode=raw_01` in the variants.yaml for this
fallback branch.

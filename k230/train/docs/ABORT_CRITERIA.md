# Abort criteria — pivot to YOLOv5n retrain

The Path A (recover the AI Cube AnchorBaseDet via .npy reconstruction)
branch aborts and the YOLOv5n retrain branch kicks in if ANY of:

1. **`inspect_npy.py` does not yield usable weights.** The 4 MB `.npy`
   is a shape=(7,) object array. If the 7 slots are anything other than
   tensors / dicts / lists-of-tensors that can be mapped to a layer
   inventory, Path A is dead.

2. **`verify_parity.py` fails** — reconstructed PyTorch float forward
   doesn't match the float-export of the same weights as ONNX (mean abs
   diff > 1e-3). This means we can't even agree with ourselves on what
   the architecture is.

3. **All three variants in the matrix fail the success criterion**:

   - `recall@IoU0.5 score>=0.05` < 0.30, OR
   - `avg_best_iou_score` < 0.05, OR
   - `avg_best_score_iou` < 0.30.

4. **Variant kmodel size > 5 MB** (rare; only if MixQuant promotes too
   many layers). Won't fit on K230D Zero comfortably with the rest of
   the firmware.

## YOLOv5n pivot path

YOLOv5n is the natural pivot because its detection head IS AnchorBaseDet-
compatible (3 anchors × (5 + num_classes) per scale, strides 8/16/32).
The existing `aicube.anchorbasedet_post_process` and the existing
deploy_config.json structure work as-is. Only the mean/std and anchors
need to be set in deploy_config.

Steps (executed by `src\train_yolov5n\`):

1. `prepare_dataset.py` — split `my_dataset` into YOLOv5 train/val (90/10)
   with the existing label format.
2. `train.py` — train YOLOv5n at 640×480 with the anchors from
   `deploy_config.json` (so we keep the same anchor priors as AI Cube)
   for ~100 epochs.
3. `export_onnx.py` — `.pt` → `.onnx` at static `[1,3,480,640]`.
4. Feed the ONNX into the existing `batch_export.py` matrix
   (v04 / v06 / v09). Same evaluation pipeline.

Estimated time: 6-12 hours on the user's hardware. After that, the rest of
the pipeline is unchanged.

## What we lose with the YOLOv5n pivot

- Native compatibility with the existing AI Cube `.npy` (it becomes
  irrelevant).
- The exact AI Cube preprocessing chain (we use a simpler `/255` only).
- Any improvements AI Cube's custom can3_5_s backbone gives over the
  vanilla YOLOv5 CSP backbone.

We do NOT lose deployment compatibility — the same MicroPython scripts work.

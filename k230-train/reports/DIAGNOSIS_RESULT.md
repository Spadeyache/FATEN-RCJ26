# Diagnosis result — final (with v3 YOLOv8 pivot result)

## TL;DR (post-v3)

**Float YOLOv8 is the best model we have. PTQ kills both architectures.**

| | float can3 (v_float32) | **float YOLOv8** | any quantized variant |
|---|---|---|---|
| recall@IoU0.5 score>=0.05 | 0.987 | **1.000** | 0.000-0.276 |
| recall@IoU0.5 score>=0.40 | 0.987 | **1.000** | 0.000-0.184 |
| avg_best_iou | 0.839 | 0.838 | ~0.5-0.6 |
| avg_best_iou_score | 0.524 | **0.772** | <0.10 |
| avg_best_score_iou | 0.532 | **0.739** | <0.022 |
| .kmodel size | 3.98 MB | 11.55 MB | 1.35-3.07 MB |

Conclusion: **deploy `exports/yolov8_existing_float/`**. The
`avg_best_score_iou = 0.739` makes it the only model whose
highest-confidence box is reliably the right one — the property the
8-byte single-best UART protocol requires.

## v3 PTQ experiments (YOLOv8 pivot)

The hypothesis was that YOLOv8's PTQ-friendlier architecture (SiLU,
no SE, wider channels) would survive uint8 quantization where can3_5_s
doesn't. We tested:

| variant | source | preprocess | PTQ | calib | recall@0.4 | avg_bis | classifier max |
|---|---|---|---|---|---|---|---|
| yolov8_existing_mix | convert_canaan MixQuant | raw_01 | Kld u8/u8 + selective i16 | 8 | 0.000 | 0.001 | 0.0004 |
| yolov8_existing_squant | convert_to_kmodel3 | raw_01 | Kld u8/u8 + Squant | 100 | 0.000 | 0.001 | 0.0011 |
| yolov8_squant_raw01_calib128 | our convert_kmodel.py | raw_01 | Kld u8/u8 + Squant | 128 | 0.000 | 0.0002 | 0.0005 |
| yolov8_existing_float | convert_to_kmodel3float | raw_01 | float32 | n/a | **1.000** | **0.772** | **0.81** |

Every quantized variant has the same pathology: **classifier head
outputs collapsed to near zero** (max post-sigmoid 0.0004-0.0011)
while bbox regression survives. Float kmodel works perfectly.

## Why both architectures fail under PTQ

It's NOT the architecture. The same `quant_error.csv` pattern is
visible on both can3 and YOLOv8: the classifier head's positive
activations (corresponding to true-positive cells) are extreme
outliers relative to the bulk of background-cell activations.

- Out of 6300 anchors × 50 images = 315 000 cells, only ~76 cells
  (≈ 0.02 %) contain a true object.
- A Kld histogram-based calibrator naturally fits its quantisation
  range to the bulk of the distribution (the 99.98 % background
  cells), and clips the rare high-activation positive cells.
- After uint8 quantisation the FG/BG contrast in the classifier head
  is gone: positive logits get crushed to ~0, negative logits also
  near 0, sigmoid spits out tiny values everywhere.

This is a **dataset-shape issue**, not an architecture issue. Even
the standard recovery techniques (Squant / AdaRound, MixQuant
promoting up to 126 layers, QAT distillation, larger calibration
sets, no-letterbox calibration) only get us to recall@0.2 ≈ 0.18 at
best — well short of usable single-best detection.

## What would actually fix the PTQ — out of scope for "a few hours"

1. **Train with a calibration-friendly objective**: add an L2 penalty
   on the classifier logits' magnitude, so float outputs are
   bounded. Standard QAT-from-scratch with INT8 in mind.
2. **Train with much more data**: 788 train images is small.
   YOLOv8n pretrained on COCO + fine-tuned on 5-10k augmented samples
   of black/silver markers would have far more "TP cell" examples
   that the calibrator can capture.
3. **Per-anchor symmetric quantisation on the head**: nncase 2.9
   doesn't expose this; would need custom postprocessing.
4. **Switch to a smaller resolution** (e.g. 320×240): fewer anchor
   cells means the FG/BG ratio is friendlier.

## Recommendation (deployable today)

1. Deploy `exports/yolov8_existing_float/model.kmodel` (11.5 MB) with
   the new `det_live_xy_yolov8.py`. Class IDs are swapped at send
   time so Teensy's `K230_BLACK = 0`, `K230_SILVER = 1` mapping
   stays intact.
2. Profile `det_image.py` (now instrumented with `ScopedTiming`
   per stage in v3 step #0) on the K230D to see where the 59 s
   end-to-end went. Likely 90 % of it is overhead (image I/O, debug
   prints, Display, MediaManager, BSP). Trim those for production.
3. If pure KPU inference is still too slow even after overhead trim:
   collect more training data (target ~5000 images via
   augmentation), retrain YOLOv8n, then try PTQ again — having a
   wider distribution of TP cells in the calibration set is the only
   PTQ knob we haven't been able to test in this time budget.

## Original diagnosis (v1 + v2) — preserved below

**Status**: PTQ-only quantization confirmed unviable for this architecture.
QAT-assisted PTQ recovers ~14x over the broken baseline but stays
~5x below float32 accuracy. **Two deployable options** are now available
depending on your priority:

- **`exports/q1p_qat_squant_kld_u8u8`** — quantized (1.35 MB) — fast on
  K230D, partial accuracy. recall@IoU0.5 score>=0.05 = 0.276 vs float
  0.987.
- **`exports/v_float32`** — float (3.98 MB) — slow on K230D (~59 s end-to-end
  including I/O / debug), full accuracy.

## What we tried (Phase A — pure PTQ)

| variant | finetune | calib_method | act | wts | calib | letterbox | recall@0.2 | avg_bis |
|---|---|---|---|---|---|---|---|---|
| v00 (AI Cube baseline) | (none) | NoClip | u8 | u8 | ? | yes | 0.013 | 0.001 |
| v04 (no Squant) | none | Kld | u8 | i16 | 16 | yes | 0.039 | 0.005 |
| v06 (no Squant) | none | Kld | u8 | u8 | 16 | yes | 0.026 | 0.004 |
| v09 MixQuant 12 | none | Kld | mixed | u8 | 16 | yes | 0.013 | 0.010 |
| v10 MixQuant 126 | none | Kld | mixed | u8 | 16 | yes | 0.026 | 0.007 |
| **q1 Squant** | UseSquant | Kld | u8 | u8 | 16 | yes | 0.000 | 0.003 |
| q2 Squant no-letter | UseSquant | Kld | u8 | u8 | 16 | no | 0.000 | 0.003 |
| q3 Squant i16-act | UseSquant | Kld | i16 | u8 | 16 | yes | 0.039 | 0.006 |

Adding Canaan's `UseSquant` (AdaRound) did not help on its own. The
architecture's HardSwish + SE blocks are structurally PTQ-hostile.

## What we tried (Phase B — QAT-assisted PTQ)

We added a PyTorch QAT step that fine-tunes the float teacher's weights
with fake-quantization in the forward path (STE on `torch.round`), then
bakes the fake-quanted weights into a plain float ONNX so nncase's
normal PTQ can compile it. No labels needed; loss is just MSE between
QAT student and float teacher outputs on 64 calibration images.

| variant | QAT steps | calib_method | act | wts | recall@0.2 | recall@0.05 | avg_bis |
|---|---|---|---|---|---|---|---|
| q1p_qat (400 steps) | 400 | Kld | u8 | u8 | **0.184** | **0.276** | 0.099 |
| q1p_qat_150_kld | 150 | Kld | u8 | u8 | 0.145 | 0.145 | 0.211 |
| q1p_qat1000 | 1000 | Kld | u8 | u8 | 0.026 | 0.039 | 0.004 |
| q0p_qat_noclip | 400 | NoClip | u8 | u8 | 0.132 | 0.132 | 0.090 |
| q0p_qat_150_noclip | 150 | NoClip | u8 | u8 | 0.171 | 0.171 | **0.231** |
| q3p_qat_kld_i16act | 400 | Kld | i16 | u8 | 0.026 | 0.039 | 0.060 |

Key observations:

- **QAT helps a lot.** `q1p_qat` (400 steps) is 14x better at recall@0.2
  than `v06` (the matching PTQ-only variant), and 20x better than `v00`.
- **More QAT is not better.** 1000 steps overfits to FQ noise and the
  baked weights degrade. 150-400 steps is the sweet spot.
- **Int16 activations don't compose with QAT.** `q3p_qat_kld_i16act`
  regresses vs `q1p_qat`. The QAT-adapted weights are tuned for uint8
  activations.
- **NoClip preserves geometry better but classification worse.**
  `q0p_qat_noclip` has the highest avg_best_iou (0.62) but lower
  recall than `q1p_qat`.
- **avg_best_score_iou stays near zero across all quantized variants.**
  Many false-positive high-score cells remain, even after QAT.

## Recommendation

Both options are now copied into `exports/`. Use the table below to pick:

| | float32 | q1p_qat (best quant) |
|---|---|---|
| recall@IoU0.5 score>=0.05 | 0.987 | 0.276 |
| recall@IoU0.5 score>=0.20 | 0.987 | 0.184 |
| recall@IoU0.5 score>=0.40 | 0.987 | 0.184 |
| avg_best_iou | 0.839 | 0.501 |
| avg_best_iou_score | 0.524 | 0.099 |
| .kmodel size | 3.98 MB | 1.35 MB |
| K230D inference (measured) | ~59 s end-to-end | likely 3-10x faster (untested yet) |

If your use case needs detection coverage and you can tolerate ~10
s/frame: **deploy `v_float32`** and accept it. The 59 s figure included
I/O + debug; pure inference is much shorter; we can profile.

If your use case needs real-time and you can tolerate ~80 % missed
detections: **deploy `q1p_qat`** and use a low confidence threshold
(~0.05) to recover what's there. False positives will be common but
real objects will mostly be flagged.

If you want the best of both: **use a two-stage pipeline** — `q1p_qat`
as a fast "is there anything?" gate, then route triggered frames through
`v_float32` for precise localization. This is a classic edge-AI
hierarchy and works well on K230D-class devices.

## Why uint8 PTQ fails on this architecture (root cause)

Empirically observed from `dump/4_TargetIndependentQuantPass/2_AssignRanges/quant_error.csv`:

- 92 of 255 layers have cosine error < 0.5 (severe), 165 < 0.99.
- Worst layers are the deep backbone (features.10/11) and the detection
  heads themselves (head.m.{0,1,2}).
- Promoting 12 or 126 layers to int16 (MixQuant) did NOT recover them —
  the damage is distributed.
- Adding `UseSquant` weight refinement did NOT recover them either —
  the fundamental issue is the dynamic range of HardSwish + SE outputs
  exceeds what uint8 can capture in a structured way.

QAT-assisted PTQ partially helps because the student's float weights
have been pushed into a basin where quantization noise is less
catastrophic. But the basin is still far from the float optimum.

To fully recover quantized accuracy on this model, the realistic paths
are (out of scope for "a few hours"):

1. **Operator surgery + full retrain**: replace HardSwish with ReLU
   and HardSigmoid with Sigmoid in the architecture, then re-train
   from scratch (or from `can3` pretrained weights) on `my_dataset`.
   12-24 h on a GPU.
2. **Proper QAT with GT labels**: use the actual YOLO loss against
   ground-truth labels during QAT instead of MSE-against-teacher.
   Better local minimum but requires the YOLO loss machinery.
3. **Distillation from a much larger float teacher** (e.g. YOLOv8m)
   that you separately train on this dataset, then distill into
   `can3_5_s`. 8-12 h on a GPU.

## Files produced

- `exports/q1p_qat_squant_kld_u8u8/model.kmodel` — best quantized
  (1.35 MB)
- `exports/q1p_qat_squant_kld_u8u8/deploy_config.json`
- `exports/v_float32/model.kmodel` — best accuracy (3.98 MB)
- `data/anchorbasedet_qat.onnx` — QAT-adapted ONNX used for q1p
- `data/anchorbasedet_qat.pt` — QAT-adapted PyTorch state-dict
- `src/qat/qat_finetune.py` — QAT training script
- `reports/variant_matrix_quant.csv` — full sweep numbers

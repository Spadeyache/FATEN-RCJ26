# Diagnosis result

**Run date**: 2026-05-17
**Status**: SUCCESS via float32 path; uint8 PTQ confirmed unviable for this architecture.

## The verdict

The AI Cube AnchorBaseDet (`can3_5_s`) model **cannot be uint8 PTQ-quantized
without severe accuracy loss** on this task. The fix is to deploy the
**float32 .kmodel** instead. This is ~3x larger (3.98 MB vs 1.35 MB) but
fits comfortably on K230D Zero and restores full accuracy.

| metric | float32 reconstructed | float32 kmodel (v_float32) | AI Cube baseline (v00) |
|---|---|---|---|
| recall@IoU0.5 score>=0.05 | 1.000 | 0.987 | 0.026 |
| recall@IoU0.5 score>=0.20 | 1.000 | 0.987 | 0.013 |
| recall@IoU0.5 score>=0.40 | 0.986 | 0.987 | 0.000 |
| avg_best_iou | 0.846 | 0.839 | 0.622 |
| avg_best_iou_score | 0.551 | 0.524 | 0.001 |
| avg_best_score_iou | 0.516 | 0.532 | 0.022 |
| .kmodel size | n/a (ONNX) | 3.98 MB | 1.35 MB |

The float32 kmodel matches the unquantized reconstructed ONNX within
~0.01 absolute on every metric -- proof that the kmodel compile path is
faithful and the only thing that was killing the AI Cube release was PTQ.

## What we tried and ruled out

The plan called for three PTQ variants. We ran those plus three more:

| variant | description | recall@0.05 | avg_bis | size |
|---|---|---|---|---|
| v00 | AI Cube baseline (Kld u8/u8, ImageNet baked) | 0.026 | 0.0010 | 1.35 MB |
| v04 | Kld d:int16 w:uint8 ImageNet baked | 0.066 | 0.0049 | 1.53 MB |
| v06 | Kld u8/u8 ImageNet baked (reproduces v00) | 0.092 | 0.0038 | 1.35 MB |
| v09 | MixQuant top-10 worst layers + heads -> int16 | 0.105 | 0.0099 | 1.36 MB |
| v10 | MixQuant: ALL layers with cosine < 0.7 -> int16 (126 layers) | 0.066 | 0.0075 | 1.47 MB |
| **v_float32** | **float32 kmodel, no PTQ** | **0.987** | **0.5245** | **3.98 MB** |

Notable: v10 promoted 126 layers to int16 and *got worse* than v09 (12
layers). This isn't a "promote one more" problem; the network is
quantization-incompatible at uint8.

## Diagnosis chain (in order of investigation)

1. **Decoded the AI Cube `.npy`** (`reports/npy_inspection.txt`).
   - 7-tuple: model_type, cfg dict, input_size, anchors, mean, std,
     state_dict(401 entries). Architecture: `can3` (MobileNetV3-Small
     variant, width_mult=0.5) + FPN/PAN neck + YOLOv5 Detect head.
   - Conclusion: full PyTorch checkpoint recoverable.

2. **Reconstructed the architecture in PyTorch** (`src/reconstruct/`).
   - All 401 state_dict keys map exactly, zero shape mismatches.
   - ORT (onnxruntime) parity with PyTorch: ~1e-4 mean abs diff.
   - Float32 ONNX recall@0.4 = 0.986. Architecture and decoder are
     verified correct.

3. **Ran 5 PTQ variants** (`exports/v04`/`v06`/`v09`/`v10`). None of
   them recovers meaningful detection accuracy. Pattern:
   - avg_best_iou is in the 0.55-0.60 range (the geometry survives)
   - avg_best_iou_score is ~0.005 (the confidence collapses)
   - Promoting more layers to int16 does NOT help meaningfully and
     can hurt.

4. **Cosine error analysis** (`quant_error.csv` from diagnose mode):
   - 92/255 layers have cosine_error < 0.5 (severe damage)
   - 126/255 layers have cosine_error < 0.7
   - 165/255 layers have cosine_error < 0.99
   - The damage is structural, not localized to a few layers. The
     final backbone layers (features.10/11) and detection heads
     (head.m.0/1/2) are the worst.

5. **Compiled float32 kmodel**. recall@0.4 = 0.987. Done.

## Why uint8 PTQ fails on this architecture

Speculation, but consistent with the evidence:

- The `can3_5_s` backbone is a MobileNetV3-Small variant with HardSwish
  and Squeeze-Excitation everywhere. Both HardSwish and SE have wide
  dynamic ranges that uint8 can't capture well.
- ImageNet normalization shifts the input distribution to be centered
  around zero with std ~1, which is the worst case for asymmetric
  uint8 dequantization (one side of zero gets one bin, the other side
  gets ~255 bins).
- The Detect head produces logits with a wide range pre-sigmoid;
  sigmoid is locally linear only in a narrow zone, so a small uint8
  bin in that zone has a hugely amplified effect on the final score.

These issues compound. MixQuant can patch a few layers but the
fundamental sensitivity is in the activation function choices, not
in any specific layer.

## What to do now

1. **Deploy `exports/v_float32/model.kmodel`** to K230D Zero. This is
   the only variant that works.
2. Copy the v_float32 directory's `model.kmodel` and `deploy_config.json`
   to `/data/k230-train/` on the K230D SD card.
3. Update `src/deploy/det_image.py` and `det_video.py` if needed --
   they should work as-is since they read deploy_config.json. The
   confidence threshold should be moved up to 0.3 or 0.4 since
   v_float32 produces calibrated scores.
4. Run `det_image.py` on a test image to confirm on-device behavior
   matches the PC evaluator (we expect ~98% detection recall).
5. Verify PC <-> K230D parity once on `pc_input.npy` (the user already
   has this file from prior testing).

## What we are NOT going to do

- The YOLOv5n retrain fallback is unnecessary; the float32 path works.
- We are not investigating uint8 PTQ further; the matrix data shows it
  is a structural mismatch.
- Quantization-aware training (QAT) might give back some of the size,
  but is out of scope for this engagement.

## Trade-offs of the float32 solution

| aspect | float32 (v_float32) | uint8 PTQ (broken) |
|---|---|---|
| accuracy | full (98.7% recall) | ~1% recall |
| size | 3.98 MB | 1.35 MB |
| inference speed | slower (estimate ~2-4x) | fastest |
| precision under quant noise | n/a | catastrophic |

The K230D Zero already runs 12 MB YOLOv8 float models in the user's
existing `output_float/` directory, so 4 MB is well within budget.
The accuracy/speed trade-off is the deciding factor and it tips
overwhelmingly toward float32 here.

# Diagnosis — what we know so far

Status: **Path A unlocked.** The AI Cube `.npy` was successfully decoded
and ported into a reconstructed PyTorch model that matches the saved
weights bit-for-bit (401/401 state-dict keys, zero shape mismatches).
ONNX export produces a graph whose output shapes match what the original
AI Cube `.kmodel` produces. The variants matrix has not yet been run
(blocked on building the Linux Docker image with the nncase-kpu plugin).

## What we ruled out

- **AI Cube source is irrecoverable**. The `.npy` *is* the model. It
  contains both the architecture config and the trained tensors as a
  pickled tuple of seven objects (model_type, cfg dict, input_size, anchors,
  mean, std, state_dict). See `../reports/npy_inspection.txt`.

- **Architecture mystery**. The `can3` backbone is essentially
  MobileNetV3-Small at width_mult=0.5 with Canaan's specific channel
  widths. Necks are YOLOv5-style FPN+PAN. Head is the standard YOLOv5
  Detect (3 anchors × (5 + num_classes), strides 8/16/32). See
  `../reports/state_dict_keys.txt`.

- **PyTorch reconstruction is faithful**. `verify_parity.py` runs the
  same input through (a) reconstructed PyTorch and (b) the ONNX export
  via onnxruntime; the mean absolute diff across all 3 outputs is
  ~1e-4, dominated by floating-point ordering effects in batchnorm
  folding. No structural problem.

- **Preprocessing is settled**. `example_projects/IR/config.json`
  confirms `mean=[0.485,0.456,0.406], std=[0.229,0.224,0.225]` ImageNet
  normalization in AI Cube's training. The corresponding nncase
  CompileOptions for our re-export is `input_type=uint8`,
  `input_range=[0,1]`, `mean=ImageNet`, `std=ImageNet`. The K230D
  ai2d feeds raw uint8 [0,255] and the kmodel internally dequantizes
  to [0,1] then applies mean/std. See `data/anchorbasedet_reconstructed.pt`
  metadata (`blob['mean']`, `blob['std']`).

## What is still uncertain

- **Whether `v04` (activation int16) closes the gap.** This is
  hypothesis 1 from the plan: the broken kmodel's symptoms (avg best-IoU
  geometry OK, but the matching candidate's score is ~0.0008) are the
  textbook signature of activation-uint8 PTQ collapsing detection-head
  logits. Activation int16 should fix it. We will know after running
  `convert/batch_export.py` in the Docker container.

- **Whether `v06` (baked-ImageNet u8/u8, same settings as AI Cube)
  reproduces the baseline failure.** If yes, that's a clean validation
  that our pipeline behaves the same as AI Cube's internal one. If our
  v06 turns out to be MEANINGFULLY BETTER than the AI Cube baseline,
  there is something AI Cube did differently we should investigate.

- **Whether `v09` (MixQuant heads → int16) is small enough.** Promoting
  3 Conv layers to int16 should add roughly 2 × (21·64 + 21·128 + 21·256)
  = 2 × ~9.5 K params = ~20 KB. The baseline kmodel is 1.3 MB so we
  expect v09 to be ~1.32 MB, well within the K230D Zero SRAM budget.

- **PC ↔ K230 parity for the rebuilt kmodels.** We verified PC/K230
  parity on the *AI Cube* baseline earlier (user reported ~match). We
  have NOT verified parity on a kmodel produced from our reconstructed
  ONNX. The first kmodel that beats the success criterion should be
  validated with `probe/pc_raw_probe.py` + `deploy/k230_raw_probe.py`
  + `eval/compare_pc_k230.py` before we trust the deployment.

## Diagnosis (priority order)

1. **Most likely**: Activation uint8 PTQ destroyed the detection-head
   logits. `v04` and `v09` test this.

2. **Less likely**: Calibration data distribution mismatch (AI Cube's
   internal calibration was small / cherry-picked / wrong domain).
   We use 16 hand-picked images from `my_dataset/train` covering both
   classes plus hard-negative backgrounds. Worth re-running with 32
   later if 16 doesn't move the needle.

3. **Unlikely**: Preprocessing mismatch. The mean/std are explicit and
   the kmodel's ai2d front-end is symmetric. PC and K230D have
   previously matched on the baseline kmodel.

4. **Ruled out for now**: RGB/BGR/gray3 channel order. User already
   tested this and got no difference.

## What "success" means

A variant beats baseline (per `docs/ABORT_CRITERIA.md`):
- `recall@IoU0.5 score>=0.05` >= 0.30 (vs baseline 0.04-0.06)
- `avg_best_iou_score` >= 0.05 (vs baseline 0.0008)
- `avg_best_score_iou` >= 0.30 (vs baseline 0.02)

If NO variant meets all three: scaffold the YOLOv5n fallback.

## What we will NOT do unless we have to

- Train a new model from scratch. The AI Cube weights ARE good (they
  evaluate well in AI Cube's own preview); the failure is the
  quantization pipeline, not the training.
- Modify `..\k230-training\` or `..\k230-inference\`. They are
  read-only references.
- Trust the AI Cube preview as evidence the deployed kmodel works. It
  evaluates the float reference, not the quantized export.

## Next concrete actions

1. (User or Claude) Wait for `k230d-convert` Docker image to finish
   building.
2. Run `convert/batch_export.py` inside the container to produce three
   re-quantized kmodels.
3. Run `eval/pc_eval_batch.py` inside the container to score them.
4. If any variant clears all three thresholds: deploy to K230D, run
   `k230_raw_probe.py` + `compare_pc_k230.py` to confirm PC/K230 parity,
   then run `det_image.py` and `det_video.py` on real targets.
5. If none clears: write `reports/DIAGNOSIS_RESULT.md` with the failed
   numbers and proceed to YOLOv5n retrain per `docs/ABORT_CRITERIA.md`.

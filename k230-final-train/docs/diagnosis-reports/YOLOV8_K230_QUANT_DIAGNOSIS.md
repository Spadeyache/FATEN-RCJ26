# YOLOv8 K230 Quantization Diagnosis

All numbers in this report come from re-running the eval and a new per-image diagnostic against the kmodels currently in `exports/` on 2026-05-19, inside the `k230-nncase` Docker container. Raw artifacts:

- Fresh per-model eval summary: [reports/pceval_fresh_summary.csv](pceval_fresh_summary.csv)
- Fresh eval JSON/CSV per model: [reports/repro_pceval/](repro_pceval/)
- Per-image quant diagnostic (13 images × 6 kmodels): [reports/diag_parts/](diag_parts/)
- Annotated images (GT in green, predictions in class color): [reports/vis_debug/<model>/<image>.jpg](vis_debug/)

Diagnostic script: [src/eval/diagnose_yolov8_quant.py](../src/eval/diagnose_yolov8_quant.py)
Render script: [src/eval/draw_yolov8_boxes.py](../src/eval/draw_yolov8_boxes.py)

---

## TL;DR

1. **The old "recall = 0.0, avg_best_iou = 0.0" numbers in `exports/yolov8_*/eval_summary.json` are wrong.** They are stale artifacts from an earlier (now-overwritten) version of [pc_eval_yolov8.py](../src/eval/pc_eval_yolov8.py). Re-running the same script on the same kmodel today gives recall ≥ 0.97 and IoU ≈ 0.79 for the PTQ models. **Treat all checked-in `eval_summary.json` files as suspect until re-evaluated.**
2. The real residual problem with u8/u8 PTQ is **class-score saturation** — every confident detection collapses to one identical score (0.5034 for the three older variants, 0.6860 for `kld_u8u8_calib74`). Localization is intact.
3. **`yolov8_kld_i16act_u8w_calib74` (newly compiled) matches float on IoU and restores score resolution.** It is the best PTQ kmodel currently available.

---

## A. Per-model results (fresh pc_eval, 20 val images, 33 GTs)

| Model | avg_best_iou | avg_best_iou_score | avg_best_score_iou | r@0.05 | r@0.4 |
|---|---|---|---|---|---|
| `yolov8_existing_float` | **0.8012** | 0.7488 | 0.7285 | 1.00 | 0.97 |
| `yolov8_existing_mix` | 0.7920 | 0.4927 | 0.7158 | 1.00 | 0.97 |
| `yolov8_existing_squant` | 0.7898 | 0.4839 | 0.7144 | 1.00 | 0.91 |
| `yolov8_squant_raw01_calib128` | 0.7860 | 0.4811 | 0.7115 | 0.97 | 0.91 |
| `yolov8_kld_u8u8_calib74` | 0.7822 | 0.6570 | 0.7041 | 1.00 | 0.97 |
| **`yolov8_kld_i16act_u8w_calib74`** | **0.8030** | **0.7047** | **0.7318** | **1.00** | **0.97** |

`avg_best_iou` = each GT's IoU with the same-class predicted box that maximizes IoU. `avg_best_iou_score` = the score that box happens to have. `avg_best_score_iou` = the IoU of the highest-scoring same-class box (this is what NMS-then-threshold actually uses on device).

Read this column-by-column:

- **avg_best_iou** says every PTQ kmodel localizes essentially as well as float. The detector survived quantization. There is no PTQ kmodel that is broken at localization.
- **avg_best_iou_score** is the gap. Float sits at 0.75. u8-act PTQ collapses around 0.48 because the cls activations get clipped into one quant bin. The i16-act PTQ recovers to 0.70.
- **avg_best_score_iou** is what matters for deployment. On device you keep the box with the highest score and apply CONF_THRESHOLD = 0.30. When all box scores are tied at 0.5034, "the box with the highest score" is whichever NMS happened to keep first — IoU drops. i16 fixes this (0.732 ≈ float's 0.728).

## B. Per-image evidence of score saturation

Sampled from [reports/diag_parts/](diag_parts/). `n_unique_top20` = how many distinct scores appear in the top 20 anchors. Float should be near 20. A model with score saturation drops toward 1.

| Image | float | exist_mix | exist_squant | squant128 | kld_u8u8_calib74 | **kld_i16act_calib74** |
|---|---|---|---|---|---|---|
| 003a746b *(calib)* | 20 (raw 0.81) | 6 (raw 0.50) | 9 (raw 0.50) | 9 (raw 0.50) | 9 (raw 0.69) | **20 (raw 0.75)** |
| 0148e2fc | 20 (0.83) | 1 (0.50) | 4 (0.50) | 5 (0.50) | 5 (0.69) | **18 (0.80)** |
| 03588042 | 18 (0.82) | 4 (0.50) | 7 (0.50) | 7 (0.50) | 7 (0.69) | **20 (0.74)** |
| 03cb1869 | 20 (0.82) | 2 (0.50) | 5 (0.50) | 1 (0.50) | 3 (0.69) | **18 (0.82)** |
| c27f5e94 *(calib)* | 20 (0.82) | 3 (0.50) | 2 (0.50) | 5 (0.50) | 5 (0.69) | **19 (0.82)** |

The u8-activation runs cluster their top-20 anchor scores into 1–9 distinct values. **0.5034 is a quantization grid line, not a meaningful confidence.** i16 activations restore an essentially float-like score distribution.

## C. Failure mode classification

| Model | Failure mode | Evidence |
|---|---|---|
| `yolov8_existing_float` | **None — reference.** | raw_max 0.49–0.85 across images, smooth, IoU 0.80. |
| `yolov8_existing_mix` | **Score collapse.** Boxes correct, ranking broken. | raw_max stuck at 0.5034 on 13/13 images; uniq_top20 1–14; IoU 0.79. |
| `yolov8_existing_squant` | **Score collapse.** Same pattern. | raw_max 0.5034 on 12/13 (one at 0.346); uniq_top20 2–17; IoU 0.79. |
| `yolov8_squant_raw01_calib128` | **Score collapse.** Same pattern. | raw_max 0.5034 on 12/13; uniq_top20 1–18; IoU 0.79. |
| `yolov8_kld_u8u8_calib74` *(new u8/u8 with 74 calib)* | **Score collapse, ceiling shifted up to 0.686.** | raw_max 0.6860 on 9/13; uniq_top20 3–16; IoU 0.78. |
| **`yolov8_kld_i16act_u8w_calib74`** *(new i16 act)* | **None — recovered.** | raw_max 0.53–0.83 (image-dependent); uniq_top20 16–20; IoU 0.80; best_score_iou matches float. |

**No model is in "real confidence-dead" mode (raw max ≈ 0).** The score head is producing strong activations everywhere; uint8 quant of activations is just compressing them onto a near-binary scale.

**"Eval/postprocess bug" was a separate, retroactive problem.** All checked-in `exports/yolov8_*/eval_summary.json` files claiming 0.0 came from a buggy earlier run of [pc_eval_yolov8.py](../src/eval/pc_eval_yolov8.py). The current script is fine — proven by re-running it against the unchanged `yolov8_squant_raw01_calib128/model.kmodel` and getting `avg_best_iou = 0.786, recall@0.05 = 0.97` instead of the file's stored 0.000. **The pc_eval_yolov8.py source under `src/eval/` is untracked in git** (`?? src/eval/pc_eval_yolov8.py`), so whatever earlier version produced the 0.0 numbers no longer exists. Don't trust the stored eval JSONs; re-run.

## D. What was actually broken in pc_eval when the 0.0 numbers were recorded

Cannot recover the buggy version directly. From the symptom pattern (every PTQ gets 0.0 across all GTs while float gets ≥0.9), the likely culprits are one of:

1. **Score floor too low**, so NMS gets thousands of equally-scored boxes, the per-class "winner" by IoU is dominated by noise and ends up far from any GT.
2. **Class index mismatch**. The dataset's [classes.txt](../../k230-training/datasets/IR-2026-05-07-18-52/classes.txt) says `0=black, 1=silver`. The current [pc_eval_yolov8.py:127](../src/eval/pc_eval_yolov8.py#L127) defaults to `--categories silver black` which prints the wrong names in the CSV, but matching is by integer index so it still computes the right IoU. An earlier version that filtered by `--categories` name instead of integer would zero out every GT.

There is a real, persistent, **CSV-display bug** in the current script regardless: column `gt_cls` shows the wrong class name. Inspect [reports/repro_pceval/yolov8_existing_float.csv](repro_pceval/yolov8_existing_float.csv) and you'll see GTs the dataset calls "black" labelled as "silver" and vice versa. The IoU/recall numbers in the JSON summary are correct.

## E. Final answers to your five questions

### 1. Was the old "super low confidence" real, or caused by eval/postprocess/metric error?

**Eval/metric error.** The kmodels were never confidence-dead. Their raw cls activations max out at 0.50–0.69 on real objects — uniformly clipped to a single quant level, but well above any sane confidence threshold. Annotated outputs in [reports/vis_debug/](vis_debug/) show every PTQ variant drawing tight boxes on the actual silver/black objects.

### 2. Is the current main problem calibration, u8 activation score saturation, or decode/eval mismatch?

**u8 activation score saturation on the final class head.** Calibration is fine (raw_01 matches the Ultralytics export — checked in the earlier turn). Decode is fine (`(cx,cy,w,h)` followed by argmax over channels 4..5 is correct). The activation dequant on the last sigmoided cls output uses one quant level for "anything above ~0.5", so NMS ranking and CONF_THRESHOLD become meaningless. **i16 activations remove the saturation entirely** without touching weights.

### 3. Which kmodel is currently best for deployment?

**`exports/yolov8_kld_i16act_u8w_calib74/model.kmodel`** — beats every other PTQ kmodel on every metric, ties float on `avg_best_iou` and `avg_best_score_iou`, restores the float-like score distribution. Size is 3.35 MB (vs 3.09 MB for u8/u8 and float).

If you cannot accept the modest size and per-inference latency increase from i16 activations, `yolov8_existing_mix` is the next best u8-act option (essentially tied with squant128 and the new calib74, but already on disk).

### 4. If none is good, what is the next fix?

i16 activation **is** the fix for the current symptom. If you want to push further (in increasing effort):

1. **Re-evaluate at higher image counts** (currently `--limit 20`). Run [pc_eval_yolov8.py](../src/eval/pc_eval_yolov8.py) over the full IR-2026-05-07-18-52 set to confirm the i16 advantage holds.
2. If i16-act compute on KPU turns out to be too slow on device, try **MixQuant promoting only the last cls Conv** — that's what's actually clipping. The existing [diagnose_quant.py](../src/convert/diagnose_quant.py) + [apply_mix_quant.py](../src/convert/apply_mix_quant.py) pipeline is set up for exactly this.
3. Only escalate to QAT or retraining if (1) and (2) both fail. Current evidence does not support that.

### 5. What exact file should I copy to the K230D, and what script should I run?

Copy these two files to `/data/k230-train/` on the device:

- `exports/yolov8_kld_i16act_u8w_calib74/model.kmodel` → `/data/k230-train/model.kmodel`
- `exports/yolov8_kld_i16act_u8w_calib74/deploy_config.json` → `/data/k230-train/deploy_config.json`

Then on the device, run [src/deploy/det_live_xy_yolov8.py](../src/deploy/det_live_xy_yolov8.py).

**Two device-side caveats before you deploy:**

- That script feeds the kmodel **CLAHE-equalized grayscale replicated to 3 channels** ([det_live_xy_yolov8.py:42](../src/deploy/det_live_xy_yolov8.py#L42), `APPLY_HISTEQ=True`). Your calibration was **RGB color, no histeq**. This is a real preprocessing mismatch that I cannot test on PC. To make device match calibration, either:
  - set `APPLY_HISTEQ=False` and feed RGB color (not gray3), **or**
  - rebuild a calibration set that is also CLAHE'd-grayscale-3ch, recompile.
- The script's `CONF_THRESHOLD = 0.30` is well below the i16 kmodel's typical confident-score floor (~0.5–0.8), so detections will fire. Tune up if you see false positives.

---

## How to reproduce

```bash
# Inside the k230-nncase container (compose at C:\Users\magic\Documents\robocup\k230\docker-compose.yml)
cd /k230-train
export PATH=/usr/local/lib/python3.10/dist-packages:$PATH

# Per-image quant diagnostic (13 images, all 6 kmodels)
for km in yolov8_existing_float yolov8_existing_mix yolov8_existing_squant \
          yolov8_squant_raw01_calib128 yolov8_kld_u8u8_calib74 \
          yolov8_kld_i16act_u8w_calib74; do
  python src/eval/diagnose_yolov8_quant.py \
    --kmodels exports/$km/model.kmodel \
    --images $(ls /tmp/diag_imgs/*.jpg) \
    --label-dir /datasets/IR-2026-05-07-18-52/labels \
    --out-dir reports/vis_debug \
    --out-json reports/diag_parts/${km}.json \
    --out-csv reports/diag_parts/${km}.csv
done

# Fresh val-set pc_eval (20 images each)
for km in yolov8_existing_float ... yolov8_kld_i16act_u8w_calib74; do
  python src/eval/pc_eval_yolov8.py \
    --kmodel exports/$km/model.kmodel \
    --images-dir /datasets/IR-2026-05-07-18-52/images \
    --labels-dir /datasets/IR-2026-05-07-18-52/labels --limit 20 \
    --out-csv reports/repro_pceval/${km}.csv \
    --eval-summary reports/repro_pceval/${km}.json
done
```

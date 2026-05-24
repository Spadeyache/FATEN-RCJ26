# Metrics interpretation

All metrics are computed against YOLO-format ground-truth labels
(`cls xc_norm yc_norm w_norm h_norm`) under IoU=0.5 matching.

## Per-image / per-GT metrics (in eval.csv)

For each ground-truth box we compute two candidates:

- **best_iou candidate** — among all decoded boxes (any score), the one with
  the highest IoU to this GT.
- **best_score candidate** — among all decoded boxes that *overlap* the GT at
  all, the one with the highest score.

Columns:

| Column | Meaning |
|---|---|
| `best_iou` | IoU of the best-IoU candidate |
| `best_iou_score` | Score (`obj * max(cls_logits)`) of that same box |
| `best_iou_obj` | Just the objectness |
| `best_iou_cls_score` | Just the max class score |
| `best_score` | Score of the best-score candidate |
| `best_score_iou` | IoU of the best-score candidate to this GT |

## Aggregate metrics (in eval_summary.json, variant_matrix.csv)

- **`recall@IoU0.5 score>=T`** — fraction of GT boxes that have ANY decoded box
  with score ≥ T AND IoU ≥ 0.5. Reported for
  T ∈ {0.003, 0.005, 0.01, 0.03, 0.05, 0.10, 0.20, 0.40}.
- **`avg_best_iou`** — mean of `best_iou` over all GTs.
- **`avg_best_iou_score`** — mean of `best_iou_score`.
- **`avg_best_score_iou`** — mean of `best_score_iou`.
- **`num_gt`** — total number of GT boxes evaluated.

## What a healthy detector looks like

| Metric | Healthy | Baseline (broken) |
|---|---|---|
| `recall@IoU0.5 score>=0.2` | ≥ 0.6 | ~0 |
| `recall@IoU0.5 score>=0.05` | ≥ 0.8 | 0.04-0.06 |
| `avg_best_iou` | ≥ 0.7 | 0.67-0.70 (already fine) |
| `avg_best_iou_score` | ≥ 0.4 | 0.0008 |
| `avg_best_score_iou` | ≥ 0.5 | 0.02 |

## Reading the diagnosis

- `avg_best_iou` high + `avg_best_iou_score` near zero
  → **head quantization damage**: geometry is fine but confidence collapsed.
  Fix: activation int16 or MixQuant.

- `avg_best_iou` low across the board (≤ 0.3)
  → **architecture or preprocessing mismatch**: the model isn't even
  localizing. Fix: reconstruct the right architecture, verify preprocessing.

- `avg_best_score_iou` high + `avg_best_iou_score` high
  → **healthy**, you've recovered the model.

- `avg_best_iou` high + `avg_best_score_iou` low + `avg_best_iou_score` high
  → **NMS / decoder issue**: the right boxes have score but a higher score
  exists somewhere else. Investigate the decoder + NMS settings.

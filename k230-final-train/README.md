# k230-final-train

One self-contained folder to reproduce the **verified** K230D YOLOv8n pipeline:
dataset → train → ONNX → kmodel (two ways) → simulate → compare → deploy.

Everything lives in **`k230_pipeline.py`** (no imports from sibling projects).
Versions are pinned to what was verified working. Preprocessing is standardized
on **RGB direct-resize** (resize to WxH, no letterbox, uint8 [0,255], with /255
baked into the kmodel via `input_range=[0,1]`, `std=[1,1,1]`).

## Run it in Docker (recommended — no venvs)

Two **prebuilt, verified** images do all the work (already on the dev machine):

| Image | Used for | Has |
|---|---|---|
| `k230d-train:latest` | train, export | GPU PyTorch + ultralytics 8.1.0 |
| `k230-nncase:latest` | convert, simulate, compare, deploy | nncase 2.9.0 + nncase-kpu + JupyterLab |

`docker-compose.yml` mounts this whole folder at `/workspace` in both, so paths
are identical everywhere. Start Docker Desktop, then from this folder:

```powershell
# convert / simulate / compare / deploy — the readable notebook
docker compose up lab            # -> http://localhost:8888/lab  open k230_pipeline.ipynb

# or fully headless (no notebook):
docker compose run --rm convert python3 k230_pipeline.py convert-all `
    models/best_640x480.onnx `
    --calib-data datasets/victim_20260621/val/images `
    --eval-data  datasets/victim_20260621/val/images `
    --num-classes 3 --data data.yaml --deploy-which searched
```

`k230_pipeline.ipynb` is the clean end-to-end notebook: one config cell, then one
thin cell per stage (`P.api_convert_search`, `P.api_convert_preset`, `P.api_compare`,
`P.api_deploy`) — all logic stays in `k230_pipeline.py`.

### Bare-metal fallback (only if you can't use Docker)

The two `requirements-*.txt` files pin the same verified versions and the script
auto-installs the matching one per stage (disable with `--no-bootstrap`). The
train and convert stacks conflict (numpy 1.24 vs 2.2), so they can't share one
interpreter — Docker avoids that entirely, which is why it's the recommended path.

## Pipeline (Docker)

```powershell
# 1. ORGANIZE — flat export -> train/val + data.yaml  (no container needed)
#   Drop your exported folder (images/ labels/ classes.txt) into raw_export/,
#   then just name the output — organize auto-finds the export:
py -3 k230_pipeline.py organize --out datasets/victim_20260621 --force
#   (already run for project-2-...-0bac386a -> datasets/victim_20260621,
#    603 train / 106 val, classes Dead/Live/Point)
#   To point at an export elsewhere instead:  --src "C:\path\to\export"

# 2. TRAIN  (GPU container)
docker compose run --rm train python k230_pipeline.py train `
    --data datasets/victim_20260621/data.yaml `
    --epochs 100 --batch 16 --img-height 480 --img-width 640 --name victim
#   -> runs/victim/weights/best.pt

# 3. EXPORT  (GPU container)
docker compose run --rm train python k230_pipeline.py export `
    runs/victim/weights/best.pt --output models --img-height 480 --img-width 640 --opset 11
#   -> models/best_640x480.onnx

# 4. CONVERT both ways + compare + deploy
#   either the notebook:   docker compose up lab   (open k230_pipeline.ipynb)
#   or headless:
docker compose run --rm convert python3 k230_pipeline.py convert-all models/best_640x480.onnx `
    --calib-data datasets/victim_20260621/val/images `
    --eval-data  datasets/victim_20260621/val/images `
    --num-samples 8 --eval-limit 20 --num-classes 3 `
    --img-height 480 --img-width 640 `
    --data data.yaml --deploy-which searched
```

## The two conversion modes

**`convert-search`** — sweeps PTQ options (default `0,3,1,4`), simulates each
kmodel against the float ONNX (cosine similarity + detection-confidence delta),
ranks them, and saves the winner to `best_model/searched/` with full
`params.json` (winning config + metrics + the whole ranking).

**`convert-preset`** — compiles the ONE predefined verified-best config
(`ptq_option 0`: NoClip, uint8 act + uint8 weights, Canaan `[0,1]`/`std[1,1,1]`)
to `best_model/preset/` with `params.json`.

**`compare`** — pits the searched winner vs the preset on **accuracy**
(cosine, conf-delta) and **speed** (sim latency, kmodel size, compile time),
writing `best_model/comparison.json` and a verdict per metric.

Run them individually if you prefer:
```powershell
py -3 k230_pipeline.py convert-search  models/best_640x480.onnx --calib-data ... --num-classes 3
py -3 k230_pipeline.py convert-preset  models/best_640x480.onnx --calib-data ... --num-classes 3
py -3 k230_pipeline.py compare
py -3 k230_pipeline.py deploy --which searched --data data.yaml
```

## Outputs

```
datasets/<name>/{train,val}/{images,labels} + data.yaml + classes.txt
runs/<name>/weights/best.pt
models/<stem>_WxH.onnx
output/search/<variant>/{model.kmodel, sim.json}     # every swept candidate
best_model/searched/{model.kmodel, params.json}      # sweep winner
best_model/preset/{model.kmodel, params.json}        # predefined best
best_model/comparison.json                           # searched vs preset
deploy/{best.kmodel, labels.txt, deploy_config.json, deploy_canmv_yolov8.py}
```

## Deploy to K230D

Copy the 4 files in `deploy/` to `/data/k230-final-train/` on the SD card, put a
`test.jpg` there too, then from the CanMV REPL:

```python
import deploy_canmv_yolov8
deploy_canmv_yolov8.detection()
```

The on-device script uses the same RGB direct-resize preprocessing the kmodel was
calibrated on, decodes the YOLOv8 anchor-free output in pure Python, runs
class-wise NMS, and saves an annotated `det_result.jpg`.

## Reproduce on another PC

1. Copy this whole folder.
2. `pip install -r requirements-train.txt` (train env) and
   `pip install -r requirements-convert.txt` (convert env) — pinned versions.
3. Run the stages above. Nothing references anything outside this folder.

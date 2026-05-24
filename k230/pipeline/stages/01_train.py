"""Stage 01: Train YOLOv8n via ultralytics.

Runs inside the `k230-train` container. Reads pipeline.yaml, writes
best.pt under artifacts/runs/<name>/weights/best.pt.

Usage:
    python stages/01_train.py            # full run (epochs from config)
    python stages/01_train.py --smoke    # 1 epoch, tiny imgsz — wiring test
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from shared.config import load

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--smoke", action="store_true", help="1-epoch smoke test")
    args = ap.parse_args()

    cfg = load()
    t = cfg["train"]

    from ultralytics import YOLO
    model = YOLO(t["model"])

    if args.smoke:
        epochs = t["smoke_epochs"]
        imgsz = t["smoke_imgsz"]
        name = f"{t['name']}_smoke"
        print(f"[stage 01] SMOKE: epochs={epochs} imgsz={imgsz}")
    else:
        epochs = t["epochs"]
        imgsz = t["imgsz"]
        name = t["name"]
        print(f"[stage 01] FULL: epochs={epochs} imgsz={imgsz}")

    results = model.train(
        data=cfg["dataset"]["data_yaml"],
        epochs=epochs,
        imgsz=imgsz if isinstance(imgsz, int) else max(imgsz),
        rect=False if args.smoke else True,
        batch=t["batch"],
        device=t["device"],
        patience=t.get("patience", 20),
        project=t["project"],
        name=name,
        exist_ok=True,
        verbose=True,
    )
    best = Path(t["project"]) / name / "weights" / "best.pt"
    print(f"[stage 01] DONE -> {best}")
    assert best.exists(), f"best.pt not found at {best}"

if __name__ == "__main__":
    main()

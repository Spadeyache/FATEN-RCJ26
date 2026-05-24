"""Stage 02: Export .pt -> ONNX (static 640x480).

Runs inside `k230-train` container.
"""
import sys, shutil
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from shared.config import load

def main():
    cfg = load()
    e = cfg["export"]
    t = cfg["train"]

    pt_path = Path(e["pt"])
    # If config points to default name and smoke run is what we have, fall back
    if not pt_path.exists():
        # Try smoke output
        smoke_pt = Path(t["project"]) / f"{t['name']}_smoke" / "weights" / "best.pt"
        if smoke_pt.exists():
            print(f"[stage 02] {pt_path} missing -- falling back to smoke run {smoke_pt}")
            pt_path = smoke_pt
        else:
            raise FileNotFoundError(f"Neither {pt_path} nor {smoke_pt} exists.")

    from ultralytics import YOLO
    model = YOLO(str(pt_path))
    out = model.export(
        format="onnx",
        imgsz=cfg["train"]["imgsz"],   # [H, W]
        opset=e["opset"],
        simplify=e["simplify"],
        dynamic=False,
    )
    out = Path(out)
    dest = Path(e["onnx"])
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy(out, dest)
    print(f"[stage 02] DONE -> {dest}  ({dest.stat().st_size/1e6:.2f} MB)")

if __name__ == "__main__":
    main()

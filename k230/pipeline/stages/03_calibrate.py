"""Stage 03: Sample N calibration images from the training set.

Runs in either container. Just file copy.
"""
import sys, random, shutil
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from shared.config import load

def main():
    cfg = load()
    c = cfg["calibrate"]
    src = Path(c["src_images"])
    out = Path(c["out_dir"])
    n = c["count"]

    out.mkdir(parents=True, exist_ok=True)
    # Clear stale
    for p in out.glob("*.jpg"):
        p.unlink()

    imgs = sorted(src.glob("*.jpg")) + sorted(src.glob("*.png"))
    if not imgs:
        raise FileNotFoundError(f"No .jpg/.png under {src}")
    random.seed(42)
    pick = random.sample(imgs, min(n, len(imgs)))
    for p in pick:
        shutil.copy(p, out / p.name)
    print(f"[stage 03] DONE: copied {len(pick)} / {len(imgs)} -> {out}")

if __name__ == "__main__":
    main()

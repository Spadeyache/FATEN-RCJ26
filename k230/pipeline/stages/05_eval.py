"""Stage 05: Evaluate every kmodel variant via nncase PC simulator.

Runs inside `k230-nncase` container.
Emits per-variant summary.json + reports/variant_matrix.csv.
"""
import sys, subprocess, json, csv
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from shared.config import load

def main():
    cfg = load()
    e = cfg["eval"]
    exports = Path(cfg["compile"]["exports_dir"])
    reports = Path(e["report_csv"]).parent
    reports.mkdir(parents=True, exist_ok=True)

    rows = []
    for variant_dir in sorted(exports.iterdir()):
        kmodel = variant_dir / "model.kmodel"
        if not kmodel.exists():
            continue
        out_csv = reports / f"eval_{variant_dir.name}.csv"
        out_json = reports / f"eval_{variant_dir.name}_summary.json"
        print(f"\n[stage 05] eval {variant_dir.name}")
        cmd = [
            "python", "/k230-train/src/eval/pc_eval_yolov8.py",
            "--kmodel", str(kmodel),
            "--images-dir", e["images_dir"],
            "--labels-dir", e["labels_dir"],
            "--model-w", str(cfg["train"]["imgsz"][1]),
            "--model-h", str(cfg["train"]["imgsz"][0]),
            "--limit", str(e["limit"]),
            "--out-csv", str(out_csv),
            "--eval-summary", str(out_json),
        ]
        subprocess.run(cmd, check=True)
        with open(out_json) as f:
            s = json.load(f)
        s["variant"] = variant_dir.name
        s["kmodel_mb"] = kmodel.stat().st_size / 1e6
        rows.append(s)

    if not rows:
        print("[stage 05] no kmodels found -- run stage 04 first")
        return

    keys = ["variant", "kmodel_mb",
            "avg_best_iou", "avg_best_iou_score",
            "avg_best_score_iou",
            "recall@IoU0.5_score>=0.05", "recall@IoU0.5_score>=0.20",
            "recall@IoU0.5_score>=0.40"]
    with open(e["report_csv"], "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(keys)
        for r in rows:
            w.writerow([r.get(k, "") for k in keys])
    print(f"\n[stage 05] DONE -> {e['report_csv']}")
    for r in rows:
        print(f"  {r['variant']:35s}  iou={r.get('avg_best_iou',0):.3f}  "
              f"r@0.05={r.get('recall@IoU0.5_score>=0.05',0):.3f}")

if __name__ == "__main__":
    main()

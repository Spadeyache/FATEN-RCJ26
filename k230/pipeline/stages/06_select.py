"""Stage 06: Pick the winning variant from the matrix.

Rule: highest avg_best_iou, tie-break on recall@0.05.
Writes /pipeline/artifacts/reports/winner.txt.
"""
import sys, csv
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from shared.config import load

def main():
    cfg = load()
    csv_path = Path(cfg["eval"]["report_csv"])
    if not csv_path.exists():
        print(f"[stage 06] no matrix at {csv_path} -- run stage 05 first")
        sys.exit(2)

    rows = list(csv.DictReader(open(csv_path)))
    def score(r):
        return (float(r["avg_best_iou"] or 0),
                float(r["recall@IoU0.5_score>=0.05"] or 0))
    rows.sort(key=score, reverse=True)
    winner = rows[0]
    print(f"\n[stage 06] ranking:")
    for i, r in enumerate(rows):
        marker = "*" if i == 0 else " "
        print(f" {marker} {r['variant']:35s}  iou={r['avg_best_iou']}  "
              f"r@0.05={r['recall@IoU0.5_score>=0.05']}  size={r['kmodel_mb']}MB")

    out = csv_path.parent / "winner.txt"
    out.write_text(winner["variant"])
    print(f"\n[stage 06] WINNER -> {winner['variant']}  (wrote {out})")

if __name__ == "__main__":
    main()

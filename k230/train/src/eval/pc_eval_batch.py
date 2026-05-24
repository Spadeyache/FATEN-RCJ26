"""Walk exports/ and run pc_eval on each variant. Emit a cross-variant matrix.

Output: reports/variant_matrix.csv with one row per variant and columns for
all recall thresholds + the avg-* metrics. Lets the user pick the best
variant at a glance.
"""
from __future__ import annotations

import argparse
import csv
import json
import os
import sys
from pathlib import Path


sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
from common.config import load_deploy_config  # noqa: E402
from eval.pc_eval import evaluate, THRESHOLDS  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exports-dir", default="exports")
    ap.add_argument("--images-dir", required=True)
    ap.add_argument("--labels-dir", required=True)
    ap.add_argument("--limit", type=int, default=50)
    ap.add_argument("--channel-order", default="rgb")
    ap.add_argument("--report", default="reports/variant_matrix.csv")
    ap.add_argument("--debug-count", type=int, default=5)
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--skip-existing", action="store_true",
                    help="Skip variants whose eval_summary.json already exists")
    args = ap.parse_args()

    variant_dirs = sorted(
        d for d in Path(args.exports_dir).iterdir()
        if d.is_dir() and (d / "deploy_config.json").exists()
    )
    if not variant_dirs:
        print(f"No variants in {args.exports_dir}", file=sys.stderr)
        sys.exit(2)

    print(f"Evaluating {len(variant_dirs)} variants ...")
    summaries = []
    for vd in variant_dirs:
        cfg_path = vd / "deploy_config.json"
        cfg = load_deploy_config(str(cfg_path))
        out_csv = vd / "eval.csv"
        es_json = vd / "eval_summary.json"
        debug = vd / "debug"
        if args.skip_existing and es_json.exists():
            with open(es_json) as f:
                summary = json.load(f)
            summary["variant"] = vd.name
            summaries.append(summary)
            print(f"  [skip] {vd.name}")
            continue
        print(f"\n--- {vd.name} ---")
        try:
            summary = evaluate(
                kmodel_path=cfg.kmodel_path,
                cfg=cfg,
                images_dir=args.images_dir,
                labels_dir=args.labels_dir,
                limit=args.limit,
                channel_order=args.channel_order,
                out_csv=str(out_csv),
                eval_summary=str(es_json),
                debug_dir=str(debug),
                debug_count=args.debug_count,
                seed=args.seed,
            )
        except Exception as e:
            print(f"FAIL {vd.name}: {e}")
            summary = {"error": str(e)}
        summary["variant"] = vd.name
        summaries.append(summary)

    # Write the matrix.
    os.makedirs(os.path.dirname(args.report) or ".", exist_ok=True)
    fields = [
        "variant", "kmodel_path", "images_evaluated", "num_gt",
        "avg_best_iou", "avg_best_iou_score", "avg_best_score_iou",
    ] + [f"recall@IoU0.5_score>={t}" for t in THRESHOLDS] + ["error"]

    with open(args.report, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        for s in summaries:
            writer.writerow(s)
    print(f"\nSaved {args.report}")

    # Print to stdout.
    print("\n" + "=" * 100)
    print(f"{'variant':<35} {'recall@0.05':>12} {'recall@0.2':>12} "
          f"{'avg_bi':>8} {'avg_bis':>8} {'avg_bsi':>8}")
    print("=" * 100)
    for s in summaries:
        if "error" in s and not s.get("num_gt"):
            print(f"{s['variant']:<35}   ERROR: {s['error']}")
            continue
        print(f"{s['variant']:<35} "
              f"{s.get('recall@IoU0.5_score>=0.05', 0):>12.3f} "
              f"{s.get('recall@IoU0.5_score>=0.2',  0):>12.3f} "
              f"{s.get('avg_best_iou', 0):>8.3f} "
              f"{s.get('avg_best_iou_score', 0):>8.4f} "
              f"{s.get('avg_best_score_iou', 0):>8.3f}")


if __name__ == "__main__":
    main()

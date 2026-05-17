"""Promote worst-cosine layers from quant_error.csv to int16 and recompile.

The diagnose_quant.py run produces:
  dump/QuantScheme.json
  dump/4_TargetIndependentQuantPass/2_AssignRanges/quant_error.csv

The CSV has `name, cosine_error, mre_error` per layer. We pick the N worst
(lowest cosine, since cosine=1 is perfect), set their DataType=int16 in
QuantScheme.json, and recompile with that scheme.

Usage:

    python3 src/convert/mixquant_from_csv.py \\
        --onnx data/anchorbasedet_reconstructed.onnx \\
        --calib-dir data/calibration \\
        --diag-dir exports/v09_raw01_mixquant__diag \\
        --output-dir exports/v09_mixquant \\
        --top-n 10 \\
        --include-heads
"""
from __future__ import annotations

import argparse
import copy
import csv
import json
import os
import sys
from pathlib import Path


sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
from convert.convert_kmodel import PTQ_OPTIONS  # noqa: E402
from convert.apply_mix_quant import compile_with_scheme, edit_scheme  # noqa: E402


def read_quant_error(csv_path: str):
    rows = []
    with open(csv_path) as f:
        reader = csv.DictReader(f)
        for r in reader:
            r = {k.strip(): v.strip() for k, v in r.items()}
            try:
                r["cosine_error"] = float(r["cosine_error"])
                r["mre_error"] = float(r["mre_error"])
            except Exception:
                continue
            rows.append(r)
    return rows


def select_promotions(rows, top_n: int, include_heads: bool,
                      max_cosine: float = 0.5):
    """Return a set of layer names to promote to int16.

    - Always include the N rows with the lowest cosine.
    - Optionally always include all `head/m.*/Conv_output_0` layers.
    - Optionally cap by `max_cosine`: don't bother promoting layers
      above this cosine (since they're already accurate).
    """
    rows_sorted = sorted(rows, key=lambda r: r["cosine_error"])
    promote = set()
    for r in rows_sorted[:top_n]:
        if r["cosine_error"] <= max_cosine:
            promote.add(r["name"])

    if include_heads:
        for r in rows:
            if r["name"].startswith("/head/m.") and r["name"].endswith("/Conv_output_0"):
                promote.add(r["name"])
    return promote


def edit_quant_scheme_inplace(scheme: dict, promote: set) -> dict:
    """Set DataType='int16' for each layer name in `promote`."""
    out = copy.deepcopy(scheme)
    n_changed = 0
    outputs = out.get("Outputs", [])
    for layer in outputs:
        if layer.get("Name") in promote:
            # nncase short names: "u8", "i16" (not "uint8"/"int16")
            layer["DataType"] = "i16"
            n_changed += 1
    print(f"Promoted {n_changed} layers (out of {len(promote)} requested)")
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--onnx", required=True)
    ap.add_argument("--calib-dir", required=True)
    ap.add_argument("--diag-dir", required=True,
                    help="Directory from diagnose_quant.py containing dump/")
    ap.add_argument("--output-dir", required=True)
    ap.add_argument("--top-n", type=int, default=10)
    ap.add_argument("--include-heads", action="store_true")
    ap.add_argument("--max-cosine", type=float, default=0.5,
                    help="Skip layers whose cosine error is above this")
    ap.add_argument("--calib-count", type=int, default=16)
    ap.add_argument("--input-width", type=int, default=640)
    ap.add_argument("--input-height", type=int, default=480)
    ap.add_argument("--ptq", type=int, default=3, choices=list(PTQ_OPTIONS.keys()))
    ap.add_argument("--preprocess-mode", default="baked_imagenet")
    ap.add_argument("--mean", type=float, nargs=3,
                    default=[0.485, 0.456, 0.406])
    ap.add_argument("--std", type=float, nargs=3,
                    default=[0.229, 0.224, 0.225])
    args = ap.parse_args()

    csv_path = os.path.join(args.diag_dir, "dump",
                             "4_TargetIndependentQuantPass",
                             "2_AssignRanges", "quant_error.csv")
    scheme_path = os.path.join(args.diag_dir, "dump", "QuantScheme.json")

    if not os.path.exists(csv_path):
        print(f"ERROR: not found: {csv_path}", file=sys.stderr)
        sys.exit(2)
    if not os.path.exists(scheme_path):
        print(f"ERROR: not found: {scheme_path}", file=sys.stderr)
        sys.exit(2)

    rows = read_quant_error(csv_path)
    promote = select_promotions(rows, args.top_n, args.include_heads,
                                 args.max_cosine)
    print(f"\nPromoting {len(promote)} layers to int16:")
    for p in sorted(promote):
        # find its cosine for the print
        cos = next((r["cosine_error"] for r in rows if r["name"] == p), None)
        print(f"  cos={cos:.4f}  {p}")

    with open(scheme_path) as f:
        scheme = json.load(f)
    edited = edit_quant_scheme_inplace(scheme, promote)
    os.makedirs(args.output_dir, exist_ok=True)
    edited_path = os.path.join(args.output_dir, "quant_scheme_edited.json")
    with open(edited_path, "w") as f:
        json.dump(edited, f, indent=2)
    print(f"\nEdited scheme -> {edited_path}")

    compile_with_scheme(
        onnx_path=args.onnx, output_dir=args.output_dir,
        calib_dir=args.calib_dir,
        input_width=args.input_width, input_height=args.input_height,
        calib_count=args.calib_count, ptq=args.ptq,
        preprocess_mode=args.preprocess_mode,
        mean_imagenet=args.mean, std_imagenet=args.std, swapRB=False,
        edited_scheme_path=edited_path,
    )


if __name__ == "__main__":
    main()

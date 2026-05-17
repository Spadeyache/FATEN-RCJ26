"""Walk configs/variants.yaml and produce one .kmodel per variant.

This script runs the configured variants in order. For each:

  - mode=direct    -> convert_kmodel.compile_one(...)
  - mode=mixquant  -> diagnose_quant on the reference variant, then
                      apply_mix_quant with selected promotions.

Each variant gets its own exports/<name>/ directory with:
  model.kmodel
  deploy_config.json
  summary.json
  compile_options.txt
  dump/  (if --dump)
"""
from __future__ import annotations

import argparse
import copy
import json
import os
import shutil
import sys
from pathlib import Path

import yaml


sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
from convert.convert_kmodel import compile_one, PTQ_OPTIONS  # noqa: E402
from convert.diagnose_quant import diagnose  # noqa: E402
from convert.apply_mix_quant import (  # noqa: E402
    select_layers_to_promote, edit_scheme, compile_with_scheme,
)


def write_deploy_config(template_path: str, out_path: str, variant: dict,
                         summary: dict, preprocess_mode: str):
    """Patch the template deploy_config.json for a variant."""
    with open(template_path) as f:
        tpl = json.load(f)

    tpl["calibrate_method"] = summary.get("calibrate_method") or tpl["calibrate_method"]
    tpl["ptq_option"] = (
        f"w:{summary.get('w_quant_type', 'uint8')} "
        f"d:{summary.get('quant_type', 'uint8')}"
    )
    tpl["kmodel_path"] = "model.kmodel"

    tpl.setdefault("_meta", {})
    tpl["_meta"]["variant"] = variant["name"]
    tpl["_meta"]["preprocess_mode"] = preprocess_mode
    tpl["_meta"]["input_range"] = summary.get("input_range")
    tpl["_meta"]["mean"] = summary.get("mean")
    tpl["_meta"]["std"] = summary.get("std")
    tpl["_meta"]["compile_seconds"] = summary.get("compile_seconds")
    tpl["_meta"]["kmodel_size_bytes"] = summary.get("kmodel_size_bytes")
    tpl["_meta"]["notes"] = variant.get("purpose", "")

    with open(out_path, "w") as f:
        json.dump(tpl, f, indent=4)


def process_variant(variant: dict, defaults: dict, exports_dir: str,
                     template: str, dump: bool):
    name = variant["name"]
    out_dir = os.path.join(exports_dir, name)
    print(f"\n{'=' * 60}\n>>> variant: {name}\n{'=' * 60}")
    print(f"purpose: {variant.get('purpose', '')}")

    onnx_path = variant.get("onnx_path") or defaults["onnx_path"]
    calib_dir = variant.get("calib_dir") or defaults["calib_dir"]
    calib_count = variant.get("calib_count", defaults["calib_count"])
    input_width = variant.get("input_width", defaults["input_width"])
    input_height = variant.get("input_height", defaults["input_height"])
    swapRB = variant.get("swapRB", defaults.get("swapRB", False))

    preprocess_mode = variant["preprocess_mode"]
    ptq = variant["ptq"]["code"]
    mode = variant.get("mode", "direct")

    # Use ImageNet stats; per-variant override is allowed but typically the
    # values are constants reflecting the training preprocessing.
    mean = variant.get("mean", [0.485, 0.456, 0.406])
    std = variant.get("std", [0.229, 0.224, 0.225])

    if mode == "direct":
        summary = compile_one(
            onnx_path=onnx_path, output_dir=out_dir, calib_dir=calib_dir,
            input_width=input_width, input_height=input_height,
            calib_count=calib_count, ptq=ptq,
            preprocess_mode=preprocess_mode,
            mean_imagenet=mean, std_imagenet=std,
            swapRB=swapRB, dump=dump,
        )
    elif mode == "mixquant":
        # Step 1: run diagnose on the reference variant if not already done.
        mq = variant["mixquant"]
        diag_variant = mq["diagnose_from"]
        diag_dir = os.path.join(exports_dir, diag_variant)
        scheme_candidates = list(Path(diag_dir).glob("**/quant_scheme*.json"))
        if not scheme_candidates:
            # Run diagnose into a sibling dir.
            diag_sib = os.path.join(exports_dir, f"{name}__diag")
            print(f"[MIX] {diag_variant} has no quant_scheme.json; "
                  f"running diagnose into {diag_sib}")
            ref_v = {"name": diag_variant, "preprocess_mode": preprocess_mode,
                     "ptq": variant["ptq"]}
            d_summary = diagnose(
                onnx_path=onnx_path, output_dir=diag_sib, calib_dir=calib_dir,
                input_width=input_width, input_height=input_height,
                calib_count=calib_count, ptq=ptq,
                preprocess_mode=preprocess_mode,
                mean_imagenet=mean, std_imagenet=std, swapRB=swapRB,
            )
            scheme_path = d_summary["quant_scheme_path"]
        else:
            scheme_path = str(scheme_candidates[0])
        if not scheme_path or not os.path.exists(scheme_path):
            print(f"[MIX] WARN: no scheme found; compiling without it.")
            scheme_path = None

        if scheme_path:
            with open(scheme_path) as f:
                scheme = json.load(f)
            promote = select_layers_to_promote(
                scheme, mq.get("promote_last_n", 3),
                mq.get("promote_worst_k", 5),
            )
            edited = edit_scheme(scheme, promote)
            os.makedirs(out_dir, exist_ok=True)
            edited_path = os.path.join(out_dir, "quant_scheme_edited.json")
            with open(edited_path, "w") as f:
                json.dump(edited, f, indent=2)
            print(f"[MIX] promoted {len(promote)} layers; scheme -> {edited_path}")
            summary = compile_with_scheme(
                onnx_path=onnx_path, output_dir=out_dir, calib_dir=calib_dir,
                input_width=input_width, input_height=input_height,
                calib_count=calib_count, ptq=ptq,
                preprocess_mode=preprocess_mode,
                mean_imagenet=mean, std_imagenet=std, swapRB=swapRB,
                edited_scheme_path=edited_path,
            )
        else:
            summary = compile_one(
                onnx_path=onnx_path, output_dir=out_dir, calib_dir=calib_dir,
                input_width=input_width, input_height=input_height,
                calib_count=calib_count, ptq=ptq,
                preprocess_mode=preprocess_mode,
                mean_imagenet=mean, std_imagenet=std,
                swapRB=swapRB, dump=dump,
            )
    else:
        raise ValueError(f"Unknown mode: {mode}")

    write_deploy_config(template, os.path.join(out_dir, "deploy_config.json"),
                         variant, summary, preprocess_mode)
    return summary


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--variants", default="configs/variants.yaml")
    ap.add_argument("--exports-dir", default="exports")
    ap.add_argument("--template", default="configs/deploy_config.template.json")
    ap.add_argument("--only", nargs="*",
                    help="Only run variants matching these names")
    ap.add_argument("--dump", action="store_true")
    args = ap.parse_args()

    with open(args.variants) as f:
        spec = yaml.safe_load(f)
    defaults = spec.get("defaults", {})
    variants = spec["variants"]
    if args.only:
        variants = [v for v in variants if v["name"] in args.only]
        print(f"Running {len(variants)} variant(s) (filtered by --only)")

    for v in variants:
        process_variant(v, defaults, args.exports_dir, args.template, args.dump)


if __name__ == "__main__":
    main()

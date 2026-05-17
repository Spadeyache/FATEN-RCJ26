"""Load AI Cube .npy weights into the reconstructed PyTorch model.

The .npy is a length-7 object array:
  [0] model_type str: 'AnchorBaseDet'
  [1] cfg dict: {num_classes, backbone, backbone_mode, version, width_mult, ...}
  [2] input_size int: 640
  [3] anchors: list of 3 lists of 6 ints
  [4] mean: list of 3 floats (ImageNet)
  [5] std:  list of 3 floats (ImageNet)
  [6] state_dict: dict[str, torch.Tensor], 401 entries

We attempt strict loading; mismatched keys/shapes are surfaced clearly.
"""
from __future__ import annotations

import argparse
import json
import os
import sys

import numpy as np


def _force_cpu_torch_loading():
    import torch
    import torch.serialization as ts
    ts.default_restore_location = lambda storage, _loc: storage
    if hasattr(ts, "_get_restore_location"):
        ts._get_restore_location = lambda *a, **kw: (lambda storage, _loc: storage)


_force_cpu_torch_loading()

# Now we can safely import torch and our model.
sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
import torch  # noqa: E402

from reconstruct.build_anchorbasedet import AnchorBaseDet  # noqa: E402


def load_npy_bundle(path: str):
    obj = np.load(path, allow_pickle=True)
    assert obj.dtype == object and obj.shape == (7,), \
        f"Expected shape=(7,) dtype=object, got shape={obj.shape} dtype={obj.dtype}"
    model_type, cfg, input_size, anchors, mean, std, state_dict = obj.tolist()
    return {
        "model_type": model_type,
        "cfg": dict(cfg),
        "input_size": int(input_size),
        "anchors": anchors,
        "mean": list(mean),
        "std": list(std),
        "state_dict": state_dict,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--npy", default=(
        r"C:\Users\magic\Downloads\k230model\AICube_V1.4_for_Windows"
        r"\AICube_for_Windows\example_projects\IR\model"
        r"\best_AnchorBaseDet_can3_5_n_20260514232500.npy"
    ))
    ap.add_argument("--out", default="data/anchorbasedet_reconstructed.pt",
                   help="Where to save the populated PyTorch model")
    ap.add_argument("--strict", action="store_true",
                   help="Fail on any missing or unexpected key")
    ap.add_argument("--report", default="reports/load_report.json")
    args = ap.parse_args()

    if not os.path.exists(args.npy):
        print(f"ERROR: not found: {args.npy}", file=sys.stderr)
        sys.exit(2)

    bundle = load_npy_bundle(args.npy)
    print(f"model_type: {bundle['model_type']}")
    print(f"cfg:        {bundle['cfg']}")
    print(f"input_size: {bundle['input_size']}")
    print(f"mean:       {bundle['mean']}")
    print(f"std:        {bundle['std']}")
    print(f"state_dict: {len(bundle['state_dict'])} keys")

    model = AnchorBaseDet(num_classes=int(bundle["cfg"]["num_classes"]))
    model_sd = model.state_dict()

    expected_keys = set(model_sd.keys())
    got_keys = set(bundle["state_dict"].keys())

    missing = sorted(expected_keys - got_keys)
    unexpected = sorted(got_keys - expected_keys)
    common = sorted(expected_keys & got_keys)
    shape_mismatch = []
    for k in common:
        a = tuple(model_sd[k].shape)
        b = tuple(bundle["state_dict"][k].shape)
        if a != b:
            shape_mismatch.append({"key": k, "model": list(a), "npy": list(b)})

    print(f"\nKey alignment:")
    print(f"  expected (model):  {len(expected_keys)}")
    print(f"  got     (.npy):    {len(got_keys)}")
    print(f"  common:            {len(common)}")
    print(f"  missing in .npy:   {len(missing)}")
    print(f"  unexpected in .npy: {len(unexpected)}")
    print(f"  shape mismatches:  {len(shape_mismatch)}")

    if missing[:10]:
        print(f"  first 10 missing: {missing[:10]}")
    if unexpected[:10]:
        print(f"  first 10 unexpected: {unexpected[:10]}")
    if shape_mismatch[:10]:
        for m in shape_mismatch[:10]:
            print(f"    {m['key']}: model={m['model']} npy={m['npy']}")

    if args.strict and (missing or unexpected or shape_mismatch):
        print("\nERROR: strict mode aborts on mismatches.", file=sys.stderr)
        sys.exit(1)

    # Best-effort load
    new_sd = dict(model_sd)
    for k in common:
        if tuple(model_sd[k].shape) == tuple(bundle["state_dict"][k].shape):
            new_sd[k] = bundle["state_dict"][k].to(dtype=model_sd[k].dtype)
    model.load_state_dict(new_sd, strict=False)
    model.eval()

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    torch.save({
        "model_state_dict": model.state_dict(),
        "cfg": bundle["cfg"],
        "anchors": bundle["anchors"],
        "mean": bundle["mean"],
        "std": bundle["std"],
        "input_size": bundle["input_size"],
    }, args.out)
    print(f"\nSaved populated model to {args.out}")

    os.makedirs(os.path.dirname(args.report) or ".", exist_ok=True)
    with open(args.report, "w") as f:
        json.dump({
            "model_keys": len(expected_keys),
            "npy_keys": len(got_keys),
            "common": len(common),
            "missing": missing,
            "unexpected": unexpected,
            "shape_mismatches": shape_mismatch,
        }, f, indent=2)
    print(f"Saved report to {args.report}")


if __name__ == "__main__":
    main()

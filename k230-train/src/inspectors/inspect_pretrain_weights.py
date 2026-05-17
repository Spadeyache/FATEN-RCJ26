"""Inspect AI Cube's bundled pretrained .pth files.

Looks for backbone_can3_5_s_od.pth, neck_can3_5_s_od.pth, head_can3_5_s_od.pth
in the AI Cube install. Prints each state_dict's key names + shapes so we can
reconstruct the architecture layer by layer.
"""
from __future__ import annotations

import argparse
import os
import sys


AICUBE_OD_DIR = (
    r"C:\Users\magic\Downloads\k230model\AICube_V1.4_for_Windows"
    r"\AICube_for_Windows\AICube\pretrain_weights\od"
)
DEFAULT_FILES = [
    "backbone_can3_5_s_od.pth",
    "neck_can3_5_s_od.pth",
    "head_can3_5_s_od.pth",
]


def describe_state_dict(name: str, sd):
    print(f"\n--- {name} ---")
    if not hasattr(sd, "items"):
        print(f"  Not a dict-like object: {type(sd).__name__}")
        return
    total_params = 0
    for k, v in sd.items():
        try:
            shape = tuple(v.shape)
            n = 1
            for d in shape:
                n *= d
            total_params += n
            dtype = str(v.dtype)
            print(f"  {k:<60s} shape={shape} dtype={dtype} numel={n}")
        except Exception as e:
            print(f"  {k:<60s} <opaque: {e}>")
    print(f"  TOTAL params: {total_params:,}")


def load_torch_state_dict(path: str):
    try:
        import torch
    except ImportError:
        print("ERROR: torch is not importable. Either install torch==1.12.1 "
              "or use Option B in docs/ENV_SETUP.md.", file=sys.stderr)
        sys.exit(2)

    blob = torch.load(path, map_location="cpu")
    if isinstance(blob, dict):
        if "state_dict" in blob and isinstance(blob["state_dict"], dict):
            return blob["state_dict"]
        if "model" in blob and hasattr(blob["model"], "state_dict"):
            return blob["model"].state_dict()
    return blob


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--dir", default=AICUBE_OD_DIR,
                   help="Folder containing the backbone/neck/head .pth files")
    p.add_argument("--files", nargs="*", default=DEFAULT_FILES)
    args = p.parse_args()

    if not os.path.isdir(args.dir):
        print(f"ERROR: directory does not exist: {args.dir}", file=sys.stderr)
        sys.exit(2)

    print("=" * 60)
    print("  inspect_pretrain_weights")
    print(f"  dir: {args.dir}")
    print("=" * 60)

    for fn in args.files:
        path = os.path.join(args.dir, fn)
        if not os.path.exists(path):
            print(f"WARN: not found: {path}")
            continue
        size_mb = os.path.getsize(path) / 1024 / 1024
        print(f"\n# {fn}   ({size_mb:.2f} MB)")
        try:
            sd = load_torch_state_dict(path)
        except Exception as e:
            print(f"ERROR: failed to load {fn}: {e}")
            continue
        describe_state_dict(fn, sd)


if __name__ == "__main__":
    main()

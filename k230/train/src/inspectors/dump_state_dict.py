"""Dump the FULL state_dict from the AI Cube .npy as a flat keys/shapes table.

Companion to inspect_npy.py which only prints the first N keys.
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


def main():
    p = argparse.ArgumentParser()
    p.add_argument("npy_path")
    p.add_argument("--out-txt", default="reports/state_dict_keys.txt")
    p.add_argument("--out-json", default="reports/state_dict_meta.json")
    args = p.parse_args()

    if not os.path.exists(args.npy_path):
        print(f"ERROR: not found: {args.npy_path}", file=sys.stderr)
        sys.exit(2)

    obj = np.load(args.npy_path, allow_pickle=True)
    assert obj.dtype == object and obj.shape == (7,)

    model_type, cfg, input_size, anchors, mean, std, state_dict = obj.tolist()

    print(f"model_type:  {model_type}")
    print(f"cfg:         {dict(cfg)}")
    print(f"input_size:  {input_size}")
    print(f"anchors:     {anchors}")
    print(f"mean:        {mean}")
    print(f"std:         {std}")
    print(f"state_dict:  {len(state_dict)} entries")

    sections = {}
    rows = []
    total_params = 0
    for k, v in state_dict.items():
        try:
            shape = tuple(v.shape)
        except Exception:
            shape = ("opaque",)
        try:
            n = 1
            for d in shape:
                n *= int(d)
            total_params += n
            dtype = str(v.dtype)
        except Exception:
            n = 0
            dtype = "opaque"
        rows.append((k, shape, dtype, n))

        prefix = k.split(".", 1)[0]
        sections.setdefault(prefix, 0)
        sections[prefix] += 1

    print(f"\nTotal params (approx): {total_params:,}")
    print(f"Top-level prefixes: {sections}")

    os.makedirs(os.path.dirname(args.out_txt) or ".", exist_ok=True)
    with open(args.out_txt, "w", encoding="utf-8") as f:
        for k, shape, dtype, n in rows:
            f.write(f"{k:<70s} {str(shape):<30s} {dtype:<20s} numel={n}\n")
    print(f"wrote {args.out_txt}")

    with open(args.out_json, "w", encoding="utf-8") as f:
        json.dump({
            "model_type": model_type,
            "cfg": dict(cfg),
            "input_size": int(input_size),
            "anchors": anchors,
            "mean": list(mean),
            "std": list(std),
            "state_dict_keys": [k for k, *_ in rows],
            "state_dict_shapes": {k: list(shape) for k, shape, *_ in rows},
            "total_params": total_params,
            "section_counts": sections,
        }, f, indent=2)
    print(f"wrote {args.out_json}")


if __name__ == "__main__":
    main()

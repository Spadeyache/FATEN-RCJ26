"""DECISION GATE: inspect the AI Cube .npy file.

The .npy file at example_projects/IR/model/*.npy is 4 MB with shape=(7,)
and dtype=object. Each of the 7 slots is some Python object pickled into
the numpy file. We need to find out WHAT each slot is to decide whether
Path A (re-export from .npy) is viable.

Possible structures (in priority order of recoverability):
  (1) [backbone_state_dict, neck_state_dict, head_state_dict, anchors,
       metadata, config, ...]  -- direct PyTorch state dicts -> Path A unlocked
  (2) [numpy_array, numpy_array, ...]  -- layer weights with no names
                                         -> Path A requires shape-matching
  (3) [bytes/str/onnx_proto, ...]      -- an embedded ONNX or other format
                                         -> directly usable
  (4) Anything else -- Path A blocked, pivot to YOLOv5n
"""
from __future__ import annotations

import argparse
import os
import sys

import numpy as np


def _force_cpu_torch_loading() -> bool:
    """Configure torch so CUDA-pickled tensors load on CPU.

    The AI Cube .npy was saved on a CUDA machine, so torch.Tensor.__reduce__
    serialised CUDA storages. On a CPU-only host the default unpickle path
    raises RuntimeError. We override _get_restore_location to force CPU.
    Returns True on success.
    """
    try:
        import torch  # noqa
        import torch.serialization as ts
        ts.default_restore_location = lambda storage, _loc: storage
        # newer torch versions
        if hasattr(ts, "_get_restore_location"):
            orig = ts._get_restore_location
            ts._get_restore_location = lambda *a, **kw: (lambda storage, _loc: storage)
        return True
    except Exception as e:  # pragma: no cover
        print(f"WARN: torch CPU-loading override failed: {e}", file=sys.stderr)
        return False


_force_cpu_torch_loading()


def short_repr(x, max_chars: int = 120) -> str:
    s = repr(x)
    return s if len(s) <= max_chars else s[:max_chars] + f"... [truncated, {len(s)} chars]"


def describe(x, indent: int = 0, depth: int = 0, max_depth: int = 4):
    pad = "  " * indent
    if depth > max_depth:
        print(f"{pad}[...max depth reached...]")
        return

    t = type(x).__name__
    if isinstance(x, np.ndarray):
        if x.dtype == object:
            print(f"{pad}<ndarray dtype=object shape={x.shape}>")
            for i, item in enumerate(x.flat):
                print(f"{pad}  [{i}]:")
                describe(item, indent + 2, depth + 1, max_depth)
        else:
            try:
                lo, hi, mn = float(x.min()), float(x.max()), float(x.mean())
                print(f"{pad}<ndarray shape={x.shape} dtype={x.dtype} "
                      f"min={lo:.4g} max={hi:.4g} mean={mn:.4g} "
                      f"size={x.size}>")
            except Exception:
                print(f"{pad}<ndarray shape={x.shape} dtype={x.dtype}>")
    elif isinstance(x, dict):
        print(f"{pad}<dict len={len(x)}>")
        for k, v in list(x.items())[:50]:
            print(f"{pad}  key={short_repr(k)}:")
            describe(v, indent + 2, depth + 1, max_depth)
        if len(x) > 50:
            print(f"{pad}  ... [{len(x) - 50} more keys]")
    elif isinstance(x, (list, tuple)):
        print(f"{pad}<{t} len={len(x)}>")
        for i, item in enumerate(x[:25]):
            print(f"{pad}  [{i}]:")
            describe(item, indent + 2, depth + 1, max_depth)
        if len(x) > 25:
            print(f"{pad}  ... [{len(x) - 25} more items]")
    elif isinstance(x, (bytes, bytearray)):
        print(f"{pad}<{t} len={len(x)} head={x[:32]!r}>")
    elif isinstance(x, str):
        print(f"{pad}<str len={len(x)}> {short_repr(x)}")
    elif isinstance(x, (int, float, bool)) or x is None:
        print(f"{pad}<{t}> {short_repr(x)}")
    else:
        # Try torch.Tensor detection without importing torch eagerly
        cls_name = type(x).__module__ + "." + type(x).__name__
        if "torch" in cls_name.lower() and "tensor" in cls_name.lower():
            try:
                shape = tuple(x.shape)
                dtype = str(x.dtype)
                print(f"{pad}<{cls_name} shape={shape} dtype={dtype}>")
            except Exception:
                print(f"{pad}<{cls_name}>")
        else:
            print(f"{pad}<{cls_name}> {short_repr(x)}")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("npy_path", help="Path to the AI Cube .npy file")
    p.add_argument("--max-depth", type=int, default=4)
    args = p.parse_args()

    if not os.path.exists(args.npy_path):
        print(f"ERROR: not found: {args.npy_path}", file=sys.stderr)
        sys.exit(2)

    size_bytes = os.path.getsize(args.npy_path)
    print("=" * 60)
    print(f"  inspect_npy: {args.npy_path}")
    print(f"  size: {size_bytes:,} bytes ({size_bytes / 1024 / 1024:.2f} MB)")
    print("=" * 60)

    try:
        obj = np.load(args.npy_path, allow_pickle=True)
    except ModuleNotFoundError as e:
        # The .npy is pickled with class references requiring missing modules.
        # Knowing WHICH modules are missing tells us about the bundle.
        print(f"INFO: np.load needs module {e.name!r} to fully unpickle.")
        print("This is itself a strong signal about .npy contents:")
        print("  easydict -> bundle contains nested config dicts")
        print("  torch    -> bundle contains PyTorch tensors")
        print("Run: py -m pip install easydict torch")
        print()
        print("DECISION: Cannot confirm structure yet. Install deps and retry.")
        sys.exit(3)
    except Exception as e:
        print(f"ERROR: np.load failed: {e!r}", file=sys.stderr)
        print("\nDECISION: .npy is unreadable.", file=sys.stderr)
        print("  -> Path A is BLOCKED. Pivot to YOLOv5n retrain.")
        sys.exit(1)

    print(f"\nTop-level: {type(obj).__name__} shape={getattr(obj, 'shape', '<scalar>')}, "
          f"dtype={getattr(obj, 'dtype', '<n/a>')}")

    if isinstance(obj, np.ndarray) and obj.dtype == object:
        try:
            arr = obj.tolist() if obj.shape != () else obj.item()
            describe(arr, max_depth=args.max_depth)
        except Exception as e:
            print(f"WARN: could not unpack object array via .tolist(): {e}")
            describe(obj, max_depth=args.max_depth)
    else:
        describe(obj, max_depth=args.max_depth)

    print()
    print("=" * 60)
    print("  DECISION RULES")
    print("=" * 60)
    print("If any slot looks like an OrderedDict / dict with string keys")
    print("  -> the model is a state_dict that can be loaded directly.")
    print("If slots are ndarrays of float32, no names")
    print("  -> we can map by shape if architecture is known.")
    print("If a slot contains ONNX bytes (b'\\x08\\x07\\x12') or a model proto")
    print("  -> direct import into nncase, skip reconstruction.")
    print("Otherwise -> Path A blocked, run train_yolov5n/.")


if __name__ == "__main__":
    main()

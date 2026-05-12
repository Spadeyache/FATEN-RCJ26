#!/usr/bin/env python3
"""
Print a short summary of every file in this workspace directory.

For .py files it extracts the module docstring. For other files it
classifies them by extension / known name. Run from anywhere:

    python summarize_workspace.py
"""

import ast
import os
from pathlib import Path

WORKSPACE = Path(__file__).resolve().parent

# Manual descriptions for non-python files and any .py without a docstring.
KNOWN = {
    "data.yaml.example":      "Example YOLO dataset config (paths, nc, class names).",
    "requirements-train.txt": "Pip dependencies for the training environment.",
    "requirements-convert.txt": "Pip dependencies for ONNX -> kmodel conversion.",
    "yolov8n.pt":             "Pretrained YOLOv8 nano weights (used as training starting point).",
    "resized_640x480.jpg":    "Test artifact: input image resized to 640x480 by testing_onnx.py.",
    "result_640x480.jpg":     "Test artifact: output of testing_onnx.py with boxes drawn.",
    ".config":                "Local config / dotfile.",
}

# Rough pipeline order so the summary reads top-to-bottom.
PIPELINE_ORDER = [
    "validate_dataset.py",
    "check_dataset.py",
    "data.yaml.example",
    "train_yolov8n.py",
    "yolov8n.pt",
    "export_to_onnx.py",
    "testing_onnx.py",
    "test_inference.py",
    "convert_to_kmodel3.py",
    "deploy_canmv.py",
    "summarize_workspace.py",
    "requirements-train.txt",
    "requirements-convert.txt",
    "resized_640x480.jpg",
    "result_640x480.jpg",
    ".config",
]


def first_doc(py_path: Path) -> str:
    """Return the module docstring (first non-empty line) or empty string."""
    try:
        tree = ast.parse(py_path.read_text(encoding="utf-8"))
        doc = ast.get_docstring(tree) or ""
        for line in doc.splitlines():
            line = line.strip()
            if line:
                return line
    except Exception:
        pass
    return ""


def describe(name: str) -> str:
    path = WORKSPACE / name
    if name.endswith(".py"):
        doc = first_doc(path)
        if doc:
            return doc
    return KNOWN.get(name, "(no description)")


def main():
    print(f"\nWorkspace: {WORKSPACE}")
    print("=" * 78)

    entries = sorted(p.name for p in WORKSPACE.iterdir() if p.name != "__pycache__")
    # Order known files first (pipeline order), then anything else.
    ordered = [n for n in PIPELINE_ORDER if n in entries]
    ordered += [n for n in entries if n not in PIPELINE_ORDER]

    width = max(len(n) for n in ordered)
    for name in ordered:
        path = WORKSPACE / name
        size = path.stat().st_size if path.is_file() else 0
        size_str = f"{size/1024:7.1f} KB" if size else "    DIR  "
        print(f"  {name:<{width}}  {size_str}  {describe(name)}")

    print("=" * 78)
    print("Pipeline (typical use):")
    print("  1. validate_dataset.py / check_dataset.py  -> audit dataset")
    print("  2. train_yolov8n.py                        -> train .pt model")
    print("  3. test_inference.py                       -> sanity check .pt")
    print("  4. export_to_onnx.py                       -> .pt -> .onnx")
    print("  5. testing_onnx.py                         -> verify .onnx on image")
    print("  6. convert_to_kmodel3.py                   -> .onnx -> .kmodel (PTQ)")
    print("  7. deploy_canmv.py                         -> run .kmodel on K230D")
    print()


if __name__ == "__main__":
    main()

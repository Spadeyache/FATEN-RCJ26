#!/usr/bin/env python3
# ============================================================================
# k230_pipeline.py  --  ALL-IN-ONE, self-contained K230D YOLOv8n pipeline.
#
# One file. No imports from sibling project folders. Each stage lazily imports
# only what that stage needs, so this script loads in either the TRAIN env or
# the CONVERT env (they have conflicting deps and must stay separate).
#
# Verified-working building blocks consolidated here (style preserved):
#   train      <- train_yolov8n.py        (ultralytics 8.1.0)
#   export     <- export_to_onnx.py       (onnx 1.15 / onnxsim)
#   convert    <- convert_to_kmodel4.py   (nncase 2.9.0, Canaan [0,1]/std[1,1,1])
#   simulate   <- nncase_base_func.py + test_kmodel.py (cosine + conf delta)
#
# Preprocessing standard for the WHOLE folder (verified RGB direct-resize):
#   RGB, resize to (W,H) NO letterbox, uint8 [0,255], NCHW.
#   Compile preprocess=True, input_range=[0,1], mean=[0,0,0], std=[1,1,1].
#
# ----------------------------------------------------------------------------
# STAGES
#   organize        Sort a flat CVAT export (images/ + labels/ + classes.txt)
#                   into train/val YOLO layout + write data.yaml.   [no heavy deps]
#   train           Train YOLOv8n -> runs/<name>/weights/best.pt    [TRAIN env]
#   export          best.pt -> models/<stem>_WxH.onnx               [TRAIN env]
#
#   convert-search  Sweep N PTQ options, SIMULATE each vs float ONNX, pick the
#                   best, save winner -> best_model/searched/.      [CONVERT env]
#   convert-preset  Compile the ONE predefined verified-best config, save ->
#                   best_model/preset/.                              [CONVERT env]
#   compare         Compare searched vs preset on SPEED + ACCURACY,
#                   write best_model/comparison.json.                [CONVERT env]
#   deploy          Bundle the chosen model -> deploy/ (kmodel + labels +
#                   deploy_config.json + on-device CanMV script).    [CONVERT env]
#
#   convert-all     convert-search + convert-preset + compare + deploy.
# ============================================================================

import argparse
import json
import math
import os
import random
import shutil
import sys
import time
from glob import glob
from pathlib import Path

HERE = Path(__file__).resolve().parent

# ----------------------------------------------------------------------------
# Predefined VERIFIED-BEST compile/PTQ config (the "preset" mode).
# This is convert_to_kmodel4.py ptq-option 0: Canaan [0,1] preprocess, NoClip,
# uint8 activations + uint8 weights. Smallest + fastest; best YOLOv8n result.
# ----------------------------------------------------------------------------
PRESET = {
    "ptq_option": 0,           # NoClip + uint8 act + uint8 weights
    "calibrate_method": "NoClip",
    "quant_type": "uint8",
    "w_quant_type": "uint8",
    "input_range": [0, 1],     # Canaan pattern (NOT [0,255])
    "mean": [0, 0, 0],
    "std": [1, 1, 1],          # Canaan pattern (NOT [255,255,255])
    "swapRB": False,
}

# PTQ option table (identical to convert_to_kmodel4.py).
#   code: (calibrate_method, activation quant_type, weight w_quant_type)
PTQ_OPTIONS = {
    0: ("NoClip", "uint8", "uint8"),
    1: ("NoClip", "uint8", "int16"),
    2: ("NoClip", "int16", "uint8"),
    3: ("Kld",    "uint8", "uint8"),
    4: ("Kld",    "uint8", "int16"),
    5: ("Kld",    "int16", "uint8"),
}

# The sweep tried by convert-search: the 5 best-performing configs (the 4 uint8
# -activation options + Kld int16-weight). int16-ACTIVATION option 2 is dropped
# (heaviest, no benefit over these). Override with --options.
DEFAULT_SEARCH_OPTIONS = [0, 3, 1, 4, 5]

IMAGE_EXTS = (".jpg", ".jpeg", ".png", ".bmp", ".webp")


# ============================================================================
# Auto-bootstrap: install the right requirements file for the stage.
# ============================================================================
def ensure_requirements(stage: str, enabled: bool = True):
    """pip install the requirements file matching the stage, once.

    stage in {"train", "convert"}. Gated by a sentinel file so it only runs
    the first time per folder. Disable with --no-bootstrap.
    """
    if not enabled:
        return
    req = HERE / f"requirements-{stage}.txt"
    if not req.is_file():
        return
    sentinel = HERE / f".bootstrapped-{stage}"
    if sentinel.is_file():
        return
    print(f"[BOOTSTRAP] Installing {req.name} (first run only)...")
    import subprocess
    rc = subprocess.call(
        [sys.executable, "-m", "pip", "install", "-r", str(req)]
    )
    if rc == 0:
        sentinel.write_text("ok\n", encoding="utf-8")
        print(f"[BOOTSTRAP] Done. (delete {sentinel.name} to reinstall)")
    else:
        print(f"[BOOTSTRAP] pip returned {rc}; continuing — fix deps if a stage fails.")


# ============================================================================
# STAGE: organize  (flat CVAT export -> YOLO train/val layout + data.yaml)
# Port of organize_ir_to_IRmy_dataset.py, self-contained, + auto data.yaml.
# ============================================================================
# Default place to drop a downloaded CVAT/Label Studio export. The whole
# exported folder (the one holding images/ labels/ classes.txt) goes in here;
# `organize` finds it automatically — no long Downloads path to type.
RAW_EXPORT_DIR = HERE / "raw_export"


def _resolve_flat_exports(src: Path) -> list:
    """Return ALL folders that directly contain images/ + labels/.

    Accepts the export folder itself, or a parent holding one or more export
    subfolders. Returning a LIST is what makes "add more data" trivial: drop
    another export folder into raw_export/ and re-run organize — every round's
    images are combined (deduped by filename). Stateless and reproducible.
    """
    src = src.expanduser().resolve()
    if not src.is_dir():
        raise SystemExit(
            f"Flat export not found: {src}\n"
            f"  Drop your exported folder(s) (with images/ labels/ classes.txt) "
            f"into {RAW_EXPORT_DIR}\n  then run:  k230_pipeline.py organize --out datasets/<name>")
    if (src / "images").is_dir() and (src / "labels").is_dir():
        return [src]
    exports = [d for d in sorted(src.iterdir())
               if d.is_dir() and (d / "images").is_dir() and (d / "labels").is_dir()]
    if not exports:
        raise SystemExit(
            f"No export with images/+labels/ found in {src}\n"
            f"  Put the exported folder there (or pass --src to point at it directly).")
    print(f"[organize] combining {len(exports)} export(s): "
          + ", ".join(d.name for d in exports))
    return exports


def cmd_organize(args):
    exports = _resolve_flat_exports(Path(args.src))
    out = Path(args.out).expanduser().resolve() if args.out else (
        HERE / "datasets" / exports[0].name)
    if not (0.0 < args.train_ratio < 1.0):
        raise SystemExit("--train-ratio must be between 0 and 1 (exclusive).")

    # CLASS-MATCH CHECK: every export's classes.txt must list the same classes
    # in the same order. Class IDs in YOLO labels are positional, so combining
    # exports with different orderings would silently mislabel everything.
    def _read_classes(p):
        f = p / "classes.txt"
        if not f.is_file():
            return None
        return [ln.strip() for ln in f.read_text(encoding="utf-8").splitlines() if ln.strip()]

    class_lists = {src.name: _read_classes(src) for src in exports}
    present = {name: cl for name, cl in class_lists.items() if cl is not None}
    if len(exports) > 1 and len(present) > 1:
        uniq = {tuple(cl) for cl in present.values()}
        if len(uniq) > 1:
            msg = "\n".join(f"    {name}: {cl}" for name, cl in present.items())
            raise SystemExit(
                "CLASS MISMATCH across exports — refusing to combine "
                "(class IDs are positional; mixing would mislabel):\n" + msg +
                "\n  Re-export with a consistent class list, or organize them "
                "into separate datasets with different --out names.")
        print(f"[organize] class-match OK across {len(present)} export(s): {next(iter(present.values()))}")

    # pair images with labels across ALL export folders; dedup by filename stem
    # (a later export re-supplying the same image/label overrides the earlier).
    pairs_by_stem = {}
    classes_src = next((src / "classes.txt" for src in exports
                        if (src / "classes.txt").is_file()), None)
    for src in exports:
        img_dir, lbl_dir = src / "images", src / "labels"
        for img_path in sorted(img_dir.iterdir()):
            if not img_path.is_file() or img_path.suffix.lower() not in IMAGE_EXTS:
                continue
            lbl_path = lbl_dir / f"{img_path.stem}.txt"
            if not lbl_path.is_file():
                print(f"WARNING: no label for image: {img_path.name}")
                continue
            pairs_by_stem[img_path.stem] = (img_path, lbl_path)
    pairs = list(pairs_by_stem.values())
    if not pairs:
        raise SystemExit("No image+label pairs found; aborting.")
    print(f"[organize] {len(pairs)} unique image+label pairs total")

    # --force regenerates train/ and val/ only. Anything else under the dataset
    # dir (notably a hand-made calibration/ folder of competition-day photos) is
    # PRESERVED so re-running add_data never wipes your calibration set.
    if args.force:
        for sub in ("train", "val"):
            if (out / sub).exists():
                shutil.rmtree(out / sub)

    rng = random.Random(args.seed)
    rng.shuffle(pairs)
    n = len(pairs)
    if n >= 2:
        n_train = max(1, min(n - 1, int(round(n * args.train_ratio))))
        train_pairs, val_pairs = pairs[:n_train], pairs[n_train:]
    else:
        train_pairs, val_pairs = pairs[:], []
        print("WARNING: only one pair found; val split will be empty.")

    def copy_pairs(plist, dest_img, dest_lbl):
        dest_img.mkdir(parents=True, exist_ok=True)
        dest_lbl.mkdir(parents=True, exist_ok=True)
        for ip, lp in plist:
            shutil.copy2(ip, dest_img / ip.name)
            shutil.copy2(lp, dest_lbl / lp.name)

    copy_pairs(train_pairs, out / "train" / "images", out / "train" / "labels")
    copy_pairs(val_pairs,   out / "val"   / "images", out / "val"   / "labels")

    # classes from classes.txt (preserve order = class id order)
    names = []
    if classes_src and classes_src.is_file():
        shutil.copy2(classes_src, out / "classes.txt")
        names = [ln.strip() for ln in classes_src.read_text(
            encoding="utf-8").splitlines() if ln.strip()]

    # write data.yaml. Use the CONTAINER path (/workspace/...) because training
    # runs inside Docker where this folder is mounted at /workspace. `train`
    # auto-rewrites it for non-/workspace cwd, so it still works bare-metal.
    data_yaml = out / "data.yaml"
    rel = out.relative_to(HERE).as_posix()        # e.g. datasets/victim_20260621
    lines = [
        f"path: /workspace/{rel}",
        "train: train/images",
        "val: val/images",
        "",
        f"nc: {len(names) if names else 0}",
        "names:",
    ]
    for i, nm in enumerate(names):
        lines.append(f"  {i}: {nm}")
    data_yaml.write_text("\n".join(lines) + "\n", encoding="utf-8")

    print(f"OK Sources: {', '.join(d.name for d in exports)}")
    print(f"OK Output : {out}")
    print(f"   train  : {len(train_pairs)} pairs")
    print(f"   val    : {len(val_pairs)} pairs")
    print(f"   classes: {names if names else '(no classes.txt found)'}")
    print(f"   wrote  : {data_yaml}")
    print(f"\nNext: train with  --data \"{data_yaml}\"")


# ============================================================================
# STAGE: train  (port of train_yolov8n.py)  [TRAIN env]
# ============================================================================
def _normalize_data_yaml(data_path):
    """Return a data.yaml whose `path` is the yaml's own parent dir (absolute).

    The dataset root always IS the data.yaml's folder, so this makes the config
    portable across host/container regardless of the stored `path:` value
    (which may be a /workspace/... or a Windows path)."""
    import yaml
    data_path = Path(data_path).resolve()
    cfg = yaml.safe_load(data_path.read_text(encoding="utf-8"))
    root = data_path.parent
    stored = Path(str(cfg.get("path", "")))
    if not stored.is_dir():
        cfg["path"] = str(root)
        fixed = root / "data.normalized.yaml"
        fixed.write_text(yaml.safe_dump(cfg, sort_keys=False, allow_unicode=True),
                         encoding="utf-8")
        print(f"[train] data.yaml path '{stored}' not found -> using {root}")
        return str(fixed)
    return str(data_path)


def cmd_train(args):
    ensure_requirements("train", not args.no_bootstrap)
    import gc
    from ultralytics import YOLO
    import torch

    if not os.path.exists(args.data):
        raise FileNotFoundError(f"Dataset config not found: {args.data}")
    data_yaml = _normalize_data_yaml(args.data)

    print("=" * 80)
    print(f"YOLOv8n Training for K230D ({args.img_width}x{args.img_height})")
    print("=" * 80)

    model = YOLO("yolov8n.pt" if not args.no_pretrained else "yolov8n.yaml")

    model.train(
        data=data_yaml,
        epochs=args.epochs,
        imgsz=(args.img_height, args.img_width),
        batch=args.batch,
        device=args.device,
        workers=args.workers,
        project=str(args.project),
        name=args.name,
        cache=args.cache,
        amp=True,
        patience=50,
        save=True,
        save_period=10,
        hsv_h=0.015, hsv_s=0.7, hsv_v=0.4,
        degrees=0.0, translate=0.1, scale=0.5, shear=0.0, perspective=0.0,
        flipud=0.0, fliplr=0.5, mosaic=1.0, mixup=0.0, copy_paste=0.0,
    )

    best = Path(args.project) / args.name / "weights" / "best.pt"
    print(f"\nBest model: {best}")

    print("\nValidating best model...")
    metrics = YOLO(str(best)).val(
        data=data_yaml, imgsz=(args.img_height, args.img_width),
        device=args.device)
    print(f"  mAP50     : {metrics.box.map50:.4f}")
    print(f"  mAP50-95  : {metrics.box.map:.4f}")
    print(f"  Precision : {metrics.box.mp:.4f}")
    print(f"  Recall    : {metrics.box.mr:.4f}")

    gc.collect()
    if torch.cuda.is_available():
        torch.cuda.empty_cache()
    print(f"\nNext: export  \"{best}\"")
    return str(best)


# ============================================================================
# STAGE: export  (port of export_to_onnx.py)  [TRAIN env]
# ============================================================================
def cmd_export(args):
    ensure_requirements("train", not args.no_bootstrap)
    import gc
    from ultralytics import YOLO
    import onnx
    import onnxsim
    import torch

    if not os.path.exists(args.model):
        raise FileNotFoundError(f"Model not found: {args.model}")
    os.makedirs(args.output, exist_ok=True)

    stem = Path(args.model).stem
    onnx_out = os.path.join(args.output, f"{stem}_{args.img_width}x{args.img_height}.onnx")

    print("=" * 80)
    print("Exporting YOLOv8n -> ONNX for K230D")
    print("=" * 80)
    print(f"  input : {args.img_width}x{args.img_height}, opset {args.opset}")
    print(f"  output: {onnx_out}")

    YOLO(args.model).export(
        format="onnx",
        imgsz=(args.img_height, args.img_width),
        opset=args.opset,
        dynamic=False,
        simplify=False,   # simplify manually below for control
    )

    exported = str(Path(args.model).parent / f"{stem}.onnx")
    if not os.path.exists(exported):
        raise RuntimeError(f"ONNX export failed; not found: {exported}")

    onnx_model = onnx.load(exported)
    if not args.no_simplify:
        print("Simplifying ONNX...")
        try:
            onnx_model, ok = onnxsim.simplify(
                onnx_model,
                input_shapes={"images": [1, 3, args.img_height, args.img_width]},
            )
            print(f"  simplify ok: {ok}")
        except Exception as e:
            print(f"  simplify failed ({e}); keeping original.")

    onnx.save(onnx_model, onnx_out)
    if exported != onnx_out and os.path.exists(exported):
        os.remove(exported)

    for t in onnx_model.graph.input:
        sh = [d.dim_value for d in t.type.tensor_type.shape.dim]
        print(f"  input  {t.name}: {sh}")
    for t in onnx_model.graph.output:
        sh = [d.dim_value for d in t.type.tensor_type.shape.dim]
        print(f"  output {t.name}: {sh}")
    print(f"  size: {os.path.getsize(onnx_out)/1024/1024:.2f} MB")

    gc.collect()
    if torch.cuda.is_available():
        torch.cuda.empty_cache()
    print(f"\nNext: convert-all  \"{onnx_out}\"  --calib-data <val/images>")

    # torch 2.1 + onnx/onnxruntime can segfault ("double free in tcache") during
    # interpreter teardown AFTER the ONNX is already saved. The file is complete
    # at this point, so exit hard with code 0 to skip the buggy C++ destructors
    # and keep the one-command flow's exit status clean.
    if getattr(_export_clean_exit, "enabled", True):
        sys.stdout.flush(); sys.stderr.flush()
        os._exit(0)
    return onnx_out


def _export_clean_exit():
    pass


_export_clean_exit.enabled = True


# ============================================================================
# CONVERT internals  (port of convert_to_kmodel4.py, RGB direct-resize)
# ============================================================================
def _round32(x):
    return int(math.ceil(x / 32.0)) * 32


def _list_calib_images(calib_dir, n):
    files = [os.path.join(calib_dir, p) for p in sorted(os.listdir(calib_dir))
             if p.lower().endswith(IMAGE_EXTS)]
    if len(files) < n:
        raise SystemExit(
            f"calibration images not enough: have {len(files)}, need {n} in {calib_dir}")
    return files[:n]


def _infer_num_classes(onnx_path):
    """YOLOv8 ONNX output is (1, 4+nc, N) -> nc = channels - 4.
    Lets convert auto-adapt when the class count changes between competitions."""
    import onnx
    m = onnx.load(onnx_path)
    shp = [d.dim_value for d in m.graph.output[0].type.tensor_type.shape.dim]
    if len(shp) == 3:
        return max(1, shp[1] - 4)
    return None


def _resolve_num_classes(args):
    nc = getattr(args, "num_classes", None)
    if nc:
        return nc
    nc = _infer_num_classes(args.onnx)
    print(f"[convert] num_classes auto-detected from ONNX: {nc}")
    return nc


def _load_chw_uint8(img_path, w, h):
    """RGB, resize to (w,h) NO letterbox, uint8 [0,255], CHW. (cv2; matches verified)."""
    import cv2
    import numpy as np
    bgr = cv2.imread(img_path)
    if bgr is None:
        raise ValueError(f"could not read {img_path}")
    img = cv2.resize(bgr, (w, h))
    img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
    return np.transpose(img.astype(np.uint8), (2, 0, 1))   # CHW


def _gen_calib_tensor_data(calib_dir, w, h, n):
    """nncase set_tensor_data format: list of [ndarray(1,3,H,W) uint8] per sample."""
    import numpy as np
    data = []
    for p in _list_calib_images(calib_dir, n):
        arr = _load_chw_uint8(p, w, h)[np.newaxis, ...]   # (1,3,H,W)
        data.append([arr])
    return data


def _compile_one(onnx_path, kmodel_out, calib_dir, w, h, n,
                 calibrate_method, quant_type, w_quant_type,
                 input_range, mean, std, swapRB, target, dump_dir):
    """Compile a single kmodel. Returns (kmodel_path, compile_seconds)."""
    import numpy as np
    import onnx
    import onnxsim
    import nncase

    os.makedirs(dump_dir, exist_ok=True)

    # 1. simplify onnx (fixed input shape)
    m = onnx.load(onnx_path)
    m = onnx.shape_inference.infer_shapes(m)
    in_all = [x.name for x in m.graph.input]
    in_init = [x.name for x in m.graph.initializer]
    in_name = (list(set(in_all) - set(in_init)) or in_all)[0]
    try:
        m, ok = onnxsim.simplify(m, overwrite_input_shapes={in_name: [1, 3, h, w]})
    except TypeError:
        m, ok = onnxsim.simplify(m, input_shapes={in_name: [1, 3, h, w]})
    assert ok, "ONNX simplify validation failed"
    simp = os.path.join(dump_dir, "simplified.onnx")
    onnx.save_model(m, simp)

    # 2. compile options — Canaan RGB direct-resize pattern
    co = nncase.CompileOptions()
    co.target = target
    co.preprocess = True
    co.swapRB = swapRB
    co.input_shape = [1, 3, h, w]
    co.input_type = "uint8"
    co.input_range = input_range
    co.mean = mean
    co.std = std
    co.input_layout = "NCHW"
    co.output_layout = "NCHW"
    co.dump_ir = False
    co.dump_asm = False
    co.dump_dir = dump_dir

    compiler = nncase.Compiler(co)
    with open(simp, "rb") as f:
        compiler.import_onnx(f.read(), nncase.ImportOptions())

    # 3. PTQ
    ptq = nncase.PTQTensorOptions()
    ptq.samples_count = n
    ptq.calibrate_method = calibrate_method
    ptq.quant_type = quant_type
    ptq.w_quant_type = w_quant_type
    ptq.set_tensor_data(_gen_calib_tensor_data(calib_dir, w, h, n))
    compiler.use_ptq(ptq)

    t0 = time.time()
    compiler.compile()
    kmodel = compiler.gencode_tobytes()
    secs = time.time() - t0

    os.makedirs(os.path.dirname(kmodel_out), exist_ok=True)
    with open(kmodel_out, "wb") as f:
        f.write(kmodel)
    return kmodel_out, secs


# ============================================================================
# SIMULATE internals  (nncase_base_func.run_kmodel + cosine + conf delta)
# ============================================================================
def _get_cosine(a, b):
    import numpy as np
    a = a.reshape(-1).astype(np.float64)
    b = b.reshape(-1).astype(np.float64)
    na, nb = np.linalg.norm(a), np.linalg.norm(b)
    if na == 0 or nb == 0:
        return 0.0
    return float(np.dot(a, b) / (na * nb))


class _FloatONNX:
    def __init__(self, onnx_path):
        import onnxruntime as ort
        self.sess = ort.InferenceSession(onnx_path, providers=["CPUExecutionProvider"])
        self.name = self.sess.get_inputs()[0].name

    def infer(self, bgr, w, h):
        import cv2
        import numpy as np
        img = cv2.resize(bgr, (w, h))
        img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
        img = img.transpose(2, 0, 1)[None].astype(np.float32) / 255.0
        return self.sess.run(None, {self.name: img})[0]


_NNCASE_PATH_DONE = False


def _ensure_nncase_on_path():
    """Add the nncase install dir to PATH so the simulator can find
    `nncase.simulator.k230.sc`. Mirrors the verified k230_simulate-EN.ipynb
    'auto set environment' cell — without this, k230 simulation fails with
    'nncase.simulator.k230.sc: not found' in some containers (e.g. k230-nncase).
    """
    global _NNCASE_PATH_DONE
    if _NNCASE_PATH_DONE:
        return
    import subprocess
    try:
        out = subprocess.run([sys.executable, "-m", "pip", "show", "nncase"],
                             capture_output=True).stdout.decode()
        loc = [ln for ln in out.splitlines() if ln.startswith("Location:")]
        if loc:
            path = loc[0].split(": ", 1)[1].strip()
            if path and path not in os.environ.get("PATH", ""):
                os.environ["PATH"] = os.environ.get("PATH", "") + os.pathsep + path
    except Exception as e:
        print(f"[sim] WARN could not add nncase to PATH: {e}")
    _NNCASE_PATH_DONE = True


class _KModelSim:
    def __init__(self, kmodel_path):
        _ensure_nncase_on_path()
        import nncase
        with open(kmodel_path, "rb") as f:
            data = f.read()
        self.sim = nncase.Simulator()
        self.sim.load_model(data)

    def infer(self, bgr, w, h):
        import nncase
        import numpy as np
        img = _load_chw_uint8_from_bgr(bgr, w, h)[None]   # (1,3,H,W) uint8
        self.sim.set_input_tensor(0, nncase.RuntimeTensor.from_numpy(img))
        t0 = time.time()
        self.sim.run()
        ms = (time.time() - t0) * 1000.0
        out = self.sim.get_output_tensor(0).to_numpy()
        return out, ms


def _load_chw_uint8_from_bgr(bgr, w, h):
    import cv2
    import numpy as np
    img = cv2.resize(bgr, (w, h))
    img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
    return np.transpose(img.astype(np.uint8), (2, 0, 1))


def _postprocess(output, conf_thresh, w, h, iou=0.15, max_boxes=50):
    """YOLOv8 decode + class-wise NMS (matches test_kmodel.py)."""
    import numpy as np
    raw = np.squeeze(output)
    if raw.ndim != 2:
        return []
    pred = raw.T if raw.shape[0] < raw.shape[1] else raw
    dets = []
    for row in pred:
        xc, yc, bw, bh = row[:4]
        scores = row[4:]
        conf = float(scores.max())
        if conf < conf_thresh:
            continue
        cls = int(scores.argmax())
        x1 = max(0, int(xc - bw / 2)); y1 = max(0, int(yc - bh / 2))
        x2 = min(w, int(xc + bw / 2)); y2 = min(h, int(yc + bh / 2))
        dets.append((x1, y1, x2, y2, conf, cls))
    return _nms(dets, iou)[:max_boxes]


def _nms(dets, iou_thr):
    if not dets:
        return []
    dets = sorted(dets, key=lambda x: x[4], reverse=True)
    keep = []
    while dets:
        best = dets.pop(0)
        keep.append(best)
        bx1, by1, bx2, by2, _, bc = best
        ba = (bx2 - bx1) * (by2 - by1)

        def iou(o):
            xi1, yi1 = max(bx1, o[0]), max(by1, o[1])
            xi2, yi2 = min(bx2, o[2]), min(by2, o[3])
            inter = max(0, xi2 - xi1) * max(0, yi2 - yi1)
            area = (o[2] - o[0]) * (o[3] - o[1])
            d = ba + area - inter
            return inter / d if d > 0 else 0
        dets = [o for o in dets if o[5] != bc or iou(o) < iou_thr]
    return keep


def _simulate(onnx_path, kmodel_path, images_dir, w, h, limit, conf, num_classes):
    """Return metrics dict: cosine (mean/min), conf delta (mean/worst), latency."""
    import numpy as np
    fmodel = _FloatONNX(onnx_path)
    kmodel = _KModelSim(kmodel_path)

    files = []
    for ext in IMAGE_EXTS:
        files.extend(glob(os.path.join(images_dir, "**", "*" + ext), recursive=True))
    files = sorted(files)[:limit]
    if not files:
        raise SystemExit(f"No eval images in {images_dir}")

    cosines, deltas, lats = [], [], []
    for fp in files:
        import cv2
        bgr = cv2.imread(fp)
        if bgr is None:
            continue
        out_f = fmodel.infer(bgr, w, h)
        out_k, ms = kmodel.infer(bgr, w, h)
        lats.append(ms)
        cosines.append(_get_cosine(out_f, out_k))

        det_f = _postprocess(out_f, conf, w, h)
        det_k = _postprocess(out_k, conf, w, h)
        bf = _best_per_class(det_f, num_classes)
        bk = _best_per_class(det_k, num_classes)
        for (cid, cf), (_, ck) in zip(bf, bk):
            deltas.append(ck - cf)

    cosines = np.array(cosines) if cosines else np.array([0.0])
    deltas = np.array(deltas) if deltas else np.array([0.0])
    lats = np.array(lats) if lats else np.array([0.0])
    return {
        "images": len(files),
        "cosine_mean": float(cosines.mean()),
        "cosine_min": float(cosines.min()),
        "conf_delta_mean": float(deltas.mean()),
        "conf_delta_worst": float(deltas.min()),
        "sim_latency_ms_mean": float(lats.mean()),
        "kmodel_size_mb": os.path.getsize(kmodel_path) / 1024 / 1024,
    }


def _best_per_class(dets, num_classes):
    best = {}
    for *_, conf, cls in dets:
        if cls not in best or conf > best[cls]:
            best[cls] = conf
    return [(c, best.get(c, 0.0)) for c in range(num_classes)]


# ============================================================================
# STAGE: convert-search  (sweep -> simulate -> pick best)  [CONVERT env]
# ============================================================================
# Fidelity gate: cosine vs float ONNX above this counts as "good enough".
# On a good nncase build every config saturates near 1.0, so cosine stops
# discriminating — then the K230D's 128MB RAM makes SIZE the deciding factor.
COSINE_GATE = 0.999


def _rank_key(e):
    """Best-config ordering for a RAM-constrained board.

    1) configs that clear the fidelity gate come first (cosine >= COSINE_GATE);
    2) among those, SMALLER kmodel wins (uint8 weights beat int16 -> ~half size);
    3) tie-break: confidence closest to float ONNX (|conf_delta|);
    4) final tie-break: higher cosine.
    Pure cosine alone is a poor metric once it saturates — a 0.0007 cosine gain
    is not worth 2x the model size or worse detection confidence."""
    m = e["metrics"]
    gated = 0 if m["cosine_mean"] >= COSINE_GATE else 1
    size_bucket = round(m["kmodel_size_mb"], 1)   # 0.1MB buckets: near-equal sizes tie
    return (gated, size_bucket, abs(m["conf_delta_mean"]), -m["cosine_mean"])


def cmd_convert_search(args):
    ensure_requirements("convert", not args.no_bootstrap)
    w = _round32(args.img_width)
    h = _round32(args.img_height)
    options = [int(x) for x in args.options.split(",")] if args.options else DEFAULT_SEARCH_OPTIONS

    search_dir = HERE / "output" / "search"
    search_dir.mkdir(parents=True, exist_ok=True)

    num_classes = _resolve_num_classes(args)
    print("=" * 70)
    print(f"CONVERT-SEARCH  sweep {options}  @ {w}x{h}")
    print("=" * 70)

    results = []
    for code in options:
        method, qt, wqt = PTQ_OPTIONS[code]
        name = f"v{code}_{method.lower()}_{qt}_{wqt}"
        vdir = search_dir / name
        kmodel = str(vdir / "model.kmodel")
        print(f"\n--- [{name}] compile (method={method} act={qt} w={wqt}) ---")
        _, secs = _compile_one(
            args.onnx, kmodel, args.calib_data, w, h, args.num_samples,
            method, qt, wqt,
            PRESET["input_range"], PRESET["mean"], PRESET["std"],
            PRESET["swapRB"], args.target, str(vdir / "tmp"))
        print(f"    compiled in {secs:.1f}s  ({os.path.getsize(kmodel)/1024/1024:.2f} MB)")

        print(f"--- [{name}] simulate vs float ONNX ---")
        m = _simulate(args.onnx, kmodel, args.eval_data or args.calib_data,
                      w, h, args.eval_limit, args.conf, num_classes)
        m["compile_seconds"] = secs
        print(f"    cosine_mean={m['cosine_mean']:.4f}  conf_delta_mean={m['conf_delta_mean']:+.3f}"
              f"  size={m['kmodel_size_mb']:.2f}MB  lat={m['sim_latency_ms_mean']:.1f}ms")

        entry = {"name": name, "ptq_option": code, "calibrate_method": method,
                 "quant_type": qt, "w_quant_type": wqt, "kmodel": kmodel,
                 "metrics": m}
        (vdir / "sim.json").write_text(json.dumps(entry, indent=2), encoding="utf-8")
        results.append(entry)

    return _finalize_search(results, w, h, args)


def _finalize_search(results, w, h, args):
    """Rank candidates, copy the winner to best_model/searched, write params."""
    results.sort(key=_rank_key)
    winner = results[0]

    out_dir = HERE / "best_model" / "searched"
    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(winner["kmodel"], out_dir / "model.kmodel")

    params = {
        "mode": "searched",
        "onnx": os.path.abspath(args.onnx) if getattr(args, "onnx", None) else "",
        "input_width": w, "input_height": h,
        "num_calib_samples": getattr(args, "num_samples", None),
        "winner": {k: winner[k] for k in
                   ("name", "ptq_option", "calibrate_method", "quant_type", "w_quant_type")},
        "compile_options": {"input_range": PRESET["input_range"],
                            "mean": PRESET["mean"], "std": PRESET["std"],
                            "swapRB": PRESET["swapRB"], "input_layout": "NCHW",
                            "input_type": "uint8", "target": getattr(args, "target", "k230")},
        "metrics": winner["metrics"],
        "sweep_ranking": [{"name": e["name"],
                           "cosine_mean": e["metrics"]["cosine_mean"],
                           "conf_delta_mean": e["metrics"]["conf_delta_mean"],
                           "size_mb": e["metrics"]["kmodel_size_mb"]} for e in results],
    }
    (out_dir / "params.json").write_text(json.dumps(params, indent=2), encoding="utf-8")

    print("\n" + "=" * 70)
    print(f"SEARCH WINNER: {winner['name']}  ->  {out_dir / 'model.kmodel'}")
    print(f"  (gate cosine>={COSINE_GATE}, then smallest size, then |conf_delta|)")
    for e in results:
        mk = "  <== winner" if e is winner else ""
        m = e["metrics"]
        print(f"  {e['name']:<28} cos={m['cosine_mean']:.4f} "
              f"conf_delta={m['conf_delta_mean']:+.3f} size={m['kmodel_size_mb']:.2f}MB{mk}")
    print(f"params -> {out_dir / 'params.json'}")
    return str(out_dir / "model.kmodel")


def cmd_rerank(args):
    """Re-select the winner from EXISTING output/search/*/sim.json (no recompile).
    Use after changing the ranking metric to avoid a full sweep recompile."""
    search_dir = HERE / "output" / "search"
    results = []
    for d in sorted(search_dir.glob("*/sim.json")):
        e = json.loads(d.read_text(encoding="utf-8"))
        # ensure the kmodel path points at the local file next to sim.json
        e["kmodel"] = str(d.parent / "model.kmodel")
        results.append(e)
    if not results:
        raise SystemExit("No output/search/*/sim.json found — run convert-search first.")
    w = results[0]["metrics"].get("_w") or args.img_width
    h = results[0]["metrics"].get("_h") or args.img_height
    return _finalize_search(results, _round32(w), _round32(h), args)


# ============================================================================
# STAGE: convert-preset  (one predefined verified-best config)  [CONVERT env]
# ============================================================================
def cmd_convert_preset(args):
    ensure_requirements("convert", not args.no_bootstrap)
    w = _round32(args.img_width)
    h = _round32(args.img_height)
    method, qt, wqt = PTQ_OPTIONS[PRESET["ptq_option"]]

    out_dir = HERE / "best_model" / "preset"
    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    kmodel = str(out_dir / "model.kmodel")

    print("=" * 70)
    print(f"CONVERT-PRESET  ptq_option={PRESET['ptq_option']} "
          f"({method} act={qt} w={wqt})  @ {w}x{h}")
    print("=" * 70)

    num_classes = _resolve_num_classes(args)
    _, secs = _compile_one(
        args.onnx, kmodel, args.calib_data, w, h, args.num_samples,
        method, qt, wqt,
        PRESET["input_range"], PRESET["mean"], PRESET["std"],
        PRESET["swapRB"], args.target, str(out_dir / "tmp"))
    print(f"compiled in {secs:.1f}s  ({os.path.getsize(kmodel)/1024/1024:.2f} MB)")

    m = _simulate(args.onnx, kmodel, args.eval_data or args.calib_data,
                  w, h, args.eval_limit, args.conf, num_classes)
    m["compile_seconds"] = secs
    print(f"cosine_mean={m['cosine_mean']:.4f}  conf_delta_mean={m['conf_delta_mean']:+.3f}"
          f"  size={m['kmodel_size_mb']:.2f}MB  lat={m['sim_latency_ms_mean']:.1f}ms")

    params = {
        "mode": "preset",
        "onnx": os.path.abspath(args.onnx),
        "input_width": w, "input_height": h,
        "num_calib_samples": args.num_samples,
        "ptq": {"ptq_option": PRESET["ptq_option"], "calibrate_method": method,
                "quant_type": qt, "w_quant_type": wqt},
        "compile_options": {"input_range": PRESET["input_range"],
                            "mean": PRESET["mean"], "std": PRESET["std"],
                            "swapRB": PRESET["swapRB"], "input_layout": "NCHW",
                            "input_type": "uint8", "target": args.target},
        "metrics": m,
    }
    (out_dir / "params.json").write_text(json.dumps(params, indent=2), encoding="utf-8")
    shutil.rmtree(out_dir / "tmp", ignore_errors=True)

    print(f"\nPRESET -> {kmodel}")
    print(f"params -> {out_dir / 'params.json'}")
    return kmodel


# ============================================================================
# STAGE: compare  (searched vs preset, speed + accuracy)  [CONVERT env]
# ============================================================================
def cmd_compare(args):
    searched = HERE / "best_model" / "searched" / "params.json"
    preset = HERE / "best_model" / "preset" / "params.json"
    if not searched.is_file() or not preset.is_file():
        raise SystemExit("Run convert-search AND convert-preset first.")

    s = json.loads(searched.read_text(encoding="utf-8"))
    p = json.loads(preset.read_text(encoding="utf-8"))
    sm, pm = s["metrics"], p["metrics"]

    def winner_of(key, higher_better):
        sv, pv = sm[key], pm[key]
        if sv == pv:
            return "tie"
        if higher_better:
            return "searched" if sv > pv else "preset"
        return "searched" if sv < pv else "preset"

    comparison = {
        "searched": {"config": s["winner"], "metrics": sm},
        "preset": {"config": p["ptq"], "metrics": pm},
        "verdict": {
            "accuracy_cosine": winner_of("cosine_mean", True),
            "accuracy_conf_delta": winner_of("conf_delta_mean", True),
            "speed_latency": winner_of("sim_latency_ms_mean", False),
            "speed_size": winner_of("kmodel_size_mb", False),
            "speed_compile": winner_of("compile_seconds", False),
        },
    }
    out = HERE / "best_model" / "comparison.json"
    out.write_text(json.dumps(comparison, indent=2), encoding="utf-8")

    def row(label, key, unit="", hb=True):
        sv, pv = sm[key], pm[key]
        win = winner_of(key, hb)
        print(f"  {label:<22} searched={sv:>10.4f}{unit}   preset={pv:>10.4f}{unit}   -> {win}")

    print("=" * 70)
    print("COMPARISON  (searched winner vs predefined preset)")
    print("=" * 70)
    print(f"  searched config: {s['winner']['name']}")
    print(f"  preset   config: ptq_option={p['ptq']['ptq_option']} "
          f"({p['ptq']['calibrate_method']} {p['ptq']['quant_type']}/{p['ptq']['w_quant_type']})")
    print("-" * 70)
    row("cosine_mean", "cosine_mean", hb=True)
    row("conf_delta_mean", "conf_delta_mean", hb=True)
    row("sim_latency_ms", "sim_latency_ms_mean", " ms", hb=False)
    row("kmodel_size_mb", "kmodel_size_mb", " MB", hb=False)
    row("compile_seconds", "compile_seconds", " s", hb=False)
    print(f"\nwrote -> {out}")
    return str(out)


# ============================================================================
# STAGE: deploy  (bundle chosen model into deploy/)  [CONVERT env]
# ============================================================================
def cmd_deploy(args):
    which = args.which
    src_dir = HERE / "best_model" / which
    params_path = src_dir / "params.json"
    kmodel = src_dir / "model.kmodel"
    if not kmodel.is_file():
        raise SystemExit(f"No model at {kmodel}; run convert-{which} first.")
    params = json.loads(params_path.read_text(encoding="utf-8")) if params_path.is_file() else {}

    labels = args.labels
    if not labels and args.data and os.path.isfile(args.data):
        import yaml
        cfg = yaml.safe_load(Path(args.data).read_text(encoding="utf-8"))
        names = cfg.get("names", {})
        if isinstance(names, dict):
            labels = [names[k] for k in sorted(names)]
        elif isinstance(names, list):
            labels = names
    if not labels:
        labels = ["class0", "class1", "class2"]

    deploy_dir = HERE / "deploy"
    deploy_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(kmodel, deploy_dir / "best.kmodel")
    (deploy_dir / "labels.txt").write_text("\n".join(labels) + "\n", encoding="utf-8")

    w = params.get("input_width", _round32(args.img_width))
    h = params.get("input_height", _round32(args.img_height))
    deploy_config = {
        "model_type": "AnchorFreeDet",
        "kmodel_path": "best.kmodel",
        "categories": labels,
        "num_classes": len(labels),
        "img_size": [w, h],
        "confidence_threshold": args.conf,
        "nms_threshold": args.iou,
        "preprocess": {"layout": "NCHW", "input_type": "uint8",
                       "input_range": PRESET["input_range"],
                       "mean": PRESET["mean"], "std": PRESET["std"],
                       "swapRB": PRESET["swapRB"], "resize": "direct (no letterbox)"},
        "_meta": {"source_mode": which,
                  "ptq": params.get("ptq", params.get("winner", {}))},
    }
    (deploy_dir / "deploy_config.json").write_text(
        json.dumps(deploy_config, indent=2), encoding="utf-8")

    print("=" * 70)
    print(f"DEPLOY bundle ({which}) -> {deploy_dir}")
    print("=" * 70)
    print(f"  best.kmodel        ({(deploy_dir/'best.kmodel').stat().st_size/1024/1024:.2f} MB)")
    print(f"  labels.txt         {labels}")
    print(f"  deploy_config.json img_size={[w,h]} conf={args.conf} nms={args.iou}")
    print(f"  deploy_canmv_yolov8.py  (copy all 3 + this script to /data on the K230D SD card)")


# ============================================================================
# STAGE: convert-all  (search + preset + compare + deploy)
# ============================================================================
def cmd_convert_all(args):
    cmd_convert_search(args)
    cmd_convert_preset(args)
    cmd_compare(args)
    if args.deploy_which:
        args.which = args.deploy_which
        args.conf = args.conf2   # deploy uses its own conf (scoring conf is done)
        cmd_deploy(args)


# ============================================================================
# STAGE: report  (HTML summary of the whole run from the JSON artifacts)
# ============================================================================
def _load_json(p):
    p = Path(p)
    return json.loads(p.read_text(encoding="utf-8")) if p.is_file() else None


def cmd_report(args):
    import html as _html

    def esc(x):
        return _html.escape(str(x))

    # gather artifacts
    sweep = []
    search_dir = HERE / "output" / "search"
    if search_dir.is_dir():
        for d in sorted(search_dir.iterdir()):
            j = _load_json(d / "sim.json")
            if j:
                sweep.append(j)
    searched = _load_json(HERE / "best_model" / "searched" / "params.json")
    preset = _load_json(HERE / "best_model" / "preset" / "params.json")
    comparison = _load_json(HERE / "best_model" / "comparison.json")

    # dataset info
    ds = _load_json  # noqa
    data_yaml = Path(args.data) if args.data else (HERE / "data.yaml")
    classes, ds_path = [], ""
    train_n = val_n = None
    if data_yaml.is_file():
        import yaml
        cfg = yaml.safe_load(data_yaml.read_text(encoding="utf-8"))
        names = cfg.get("names", {})
        classes = ([names[k] for k in sorted(names)] if isinstance(names, dict)
                   else list(names) if isinstance(names, list) else [])
        ds_path = cfg.get("path", "")
        # data.yaml may store a container path (/workspace/...) that doesn't
        # exist on the host; the dataset root always IS the data.yaml's folder.
        root = Path(ds_path) if Path(ds_path).is_dir() else data_yaml.resolve().parent
        ds_path = str(root)
        for split, key in (("train", "train"), ("val", "val")):
            d = root / cfg.get(key, f"{split}/images")
            if d.is_dir():
                n = sum(1 for _ in d.glob("*") if _.suffix.lower() in IMAGE_EXTS)
                if split == "train":
                    train_n = n
                else:
                    val_n = n

    winner_name = (searched or {}).get("winner", {}).get("name", "?")

    # sweep table rows, ranked by cosine
    sweep_sorted = sorted(sweep, key=lambda e: -e["metrics"]["cosine_mean"]) if sweep else []
    rows = ""
    for e in sweep_sorted:
        m = e["metrics"]
        is_win = e["name"] == winner_name
        rows += (
            f"<tr class='{'win' if is_win else ''}'>"
            f"<td>{esc(e['name'])}{' 🏆' if is_win else ''}</td>"
            f"<td>{e['ptq_option']}</td><td>{esc(e['calibrate_method'])}</td>"
            f"<td>{esc(e['quant_type'])}/{esc(e['w_quant_type'])}</td>"
            f"<td class='num'>{m['cosine_mean']:.4f}</td>"
            f"<td class='num'>{m['conf_delta_mean']:+.3f}</td>"
            f"<td class='num'>{m['kmodel_size_mb']:.2f}</td>"
            f"<td class='num'>{m.get('compile_seconds', 0):.1f}</td>"
            f"<td class='num'>{m['sim_latency_ms_mean']:.0f}</td></tr>")

    # comparison table
    cmp_rows = ""
    if comparison:
        sm = comparison["searched"]["metrics"]
        pm = comparison["preset"]["metrics"]
        v = comparison["verdict"]
        def crow(label, key, fmt, vk):
            return (f"<tr><td>{label}</td><td class='num'>{fmt.format(sm[key])}</td>"
                    f"<td class='num'>{fmt.format(pm[key])}</td><td>{esc(v[vk])}</td></tr>")
        cmp_rows = (
            crow("cosine_mean", "cosine_mean", "{:.4f}", "accuracy_cosine") +
            crow("conf_delta_mean", "conf_delta_mean", "{:+.3f}", "accuracy_conf_delta") +
            crow("sim_latency_ms", "sim_latency_ms_mean", "{:.0f}", "speed_latency") +
            crow("kmodel_size_mb", "kmodel_size_mb", "{:.2f}", "speed_size") +
            crow("compile_seconds", "compile_seconds", "{:.1f}", "speed_compile"))

    nncase_ver = ""
    try:
        import subprocess
        out = subprocess.run([sys.executable, "-m", "pip", "show", "nncase"],
                             capture_output=True).stdout.decode()
        nncase_ver = next((l.split(": ", 1)[1] for l in out.splitlines()
                           if l.startswith("Version:")), "")
    except Exception:
        pass

    best_metrics = (searched or {}).get("metrics", {})
    html_doc = f"""<!doctype html><html lang="ja"><head><meta charset="utf-8">
<title>K230D YOLOv8n パイプライン レポート</title>
<style>
 body{{font-family:'Segoe UI',Meiryo,system-ui,sans-serif;margin:0;background:#0f172a;color:#e2e8f0}}
 .wrap{{max-width:960px;margin:0 auto;padding:32px}}
 h1{{font-size:24px;margin:0 0 4px}} h2{{font-size:18px;margin:28px 0 10px;color:#7dd3fc;border-bottom:1px solid #334155;padding-bottom:6px}}
 .sub{{color:#94a3b8;font-size:13px}}
 .cards{{display:flex;gap:12px;flex-wrap:wrap;margin:16px 0}}
 .card{{background:#1e293b;border:1px solid #334155;border-radius:10px;padding:14px 18px;min-width:150px}}
 .card .k{{color:#94a3b8;font-size:12px}} .card .v{{font-size:20px;font-weight:600;margin-top:4px}}
 table{{width:100%;border-collapse:collapse;margin:8px 0;font-size:13px}}
 th,td{{border:1px solid #334155;padding:7px 10px;text-align:left}}
 th{{background:#1e293b;color:#7dd3fc}} td.num{{text-align:right;font-variant-numeric:tabular-nums}}
 tr.win{{background:#14532d}} tr.win td{{color:#bbf7d0;font-weight:600}}
 .pill{{display:inline-block;background:#0ea5e9;color:#001;border-radius:999px;padding:2px 10px;font-size:12px;font-weight:600}}
 .note{{background:#1e293b;border-left:3px solid #0ea5e9;padding:10px 14px;border-radius:6px;font-size:13px;color:#cbd5e1;margin:10px 0}}
 code{{background:#0b1220;padding:1px 6px;border-radius:4px;color:#fde68a}}
 footer{{color:#64748b;font-size:12px;margin-top:30px}}
</style></head><body><div class="wrap">

<h1>K230D YOLOv8n パイプライン レポート 🇯🇵</h1>
<div class="sub">生成: {esc(time.strftime('%Y-%m-%d %H:%M'))} ・ nncase {esc(nncase_ver or '?')} ・ target k230</div>

<h2>1. データセット / Dataset</h2>
<div class="cards">
 <div class="card"><div class="k">クラス数 (nc)</div><div class="v">{len(classes)}</div></div>
 <div class="card"><div class="k">クラス / classes</div><div class="v" style="font-size:15px">{esc(', '.join(classes) or '?')}</div></div>
 <div class="card"><div class="k">train 画像</div><div class="v">{train_n if train_n is not None else '?'}</div></div>
 <div class="card"><div class="k">val 画像</div><div class="v">{val_n if val_n is not None else '?'}</div></div>
</div>
<div class="note">パス: <code>{esc(ds_path)}</code></div>

<h2>2. 最良の量子化設定 / Best PTQ config &nbsp;<span class="pill">{esc(winner_name)}</span></h2>
<div class="cards">
 <div class="card"><div class="k">cosine (vs float ONNX)</div><div class="v">{best_metrics.get('cosine_mean', float('nan')):.4f}</div></div>
 <div class="card"><div class="k">conf_delta</div><div class="v">{best_metrics.get('conf_delta_mean', float('nan')):+.3f}</div></div>
 <div class="card"><div class="k">サイズ size</div><div class="v">{best_metrics.get('kmodel_size_mb', float('nan')):.2f} MB</div></div>
 <div class="card"><div class="k">compile</div><div class="v">{best_metrics.get('compile_seconds', float('nan')):.1f} s</div></div>
</div>

<h2>3. PTQ スイープ結果 / Sweep (ranked by cosine)</h2>
<table><thead><tr><th>variant</th><th>opt</th><th>calib</th><th>act/w</th>
<th>cosine↑</th><th>conf_delta↑</th><th>size MB↓</th><th>compile s↓</th><th>sim ms↓</th></tr></thead>
<tbody>{rows or '<tr><td colspan=9>スイープ結果なし — convert-search を実行してください</td></tr>'}</tbody></table>
<div class="note">cosine が高いほど float ONNX と一致（量子化劣化が小さい）。conf_delta は検出信頼度の差（0 に近いほど良い）。sim ms は CPU シミュレータ速度であり実機 KPU 速度ではない（速度比較は size と compile を参照）。</div>

<h2>4. searched vs preset 比較 / Comparison</h2>
<table><thead><tr><th>metric</th><th>searched</th><th>preset</th><th>勝ち / winner</th></tr></thead>
<tbody>{cmp_rows or '<tr><td colspan=4>比較なし — convert-search と convert-preset と compare を実行</td></tr>'}</tbody></table>

<footer>k230-final-train ・ 1 スクリプト (k230_pipeline.py) + 1 ノートブック ・ venv 不使用・Docker 実行</footer>
</div></body></html>"""

    out = Path(args.output) if args.output else (HERE / "report.html")
    out.write_text(html_doc, encoding="utf-8")
    print(f"HTML report -> {out}")
    print(f"  best config : {winner_name}")
    if best_metrics:
        print(f"  cosine={best_metrics.get('cosine_mean', float('nan')):.4f}"
              f"  conf_delta={best_metrics.get('conf_delta_mean', float('nan')):+.3f}"
              f"  size={best_metrics.get('kmodel_size_mb', float('nan')):.2f}MB")
    return str(out)


# ============================================================================
# Notebook API  --  thin kwargs wrappers over the verified cmd_* functions so
# notebook cells stay one-liners. Single source of truth (no duplicated logic).
# ============================================================================
def _ns(**kw):
    return argparse.Namespace(**kw)


def api_organize(out, src=None, train_ratio=0.85, seed=42, force=True):
    return cmd_organize(_ns(src=src or str(RAW_EXPORT_DIR), out=out,
                            train_ratio=train_ratio, seed=seed, force=force))


def api_convert_search(onnx, calib_data, eval_data=None, num_classes=None,
                       img_width=640, img_height=480, num_samples=8,
                       eval_limit=20, conf=0.05, options=None, target="k230"):
    return cmd_convert_search(_ns(
        onnx=onnx, calib_data=calib_data, eval_data=eval_data,
        num_classes=num_classes, img_width=img_width, img_height=img_height,
        num_samples=num_samples, eval_limit=eval_limit, conf=conf,
        options=options, target=target, no_bootstrap=True))


def api_convert_preset(onnx, calib_data, eval_data=None, num_classes=None,
                       img_width=640, img_height=480, num_samples=8,
                       eval_limit=20, conf=0.05, target="k230"):
    return cmd_convert_preset(_ns(
        onnx=onnx, calib_data=calib_data, eval_data=eval_data,
        num_classes=num_classes, img_width=img_width, img_height=img_height,
        num_samples=num_samples, eval_limit=eval_limit, conf=conf,
        target=target, no_bootstrap=True))


def api_compare():
    return cmd_compare(_ns())


def api_report(data=None, output=None):
    return cmd_report(_ns(data=data or str(HERE / "data.yaml"), output=output))


def api_deploy(which="searched", data=None, labels=None,
               img_width=640, img_height=480, conf=0.30, iou=0.45):
    return cmd_deploy(_ns(which=which, data=data or str(HERE / "data.yaml"),
                          labels=labels, img_width=img_width,
                          img_height=img_height, conf=conf, iou=iou))


# ============================================================================
# CLI
# ============================================================================
def build_parser():
    p = argparse.ArgumentParser(
        description="All-in-one K230D YOLOv8n pipeline (organize/train/export/convert/compare/deploy)",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    p.add_argument("--no-bootstrap", action="store_true",
                   help="Do NOT auto pip-install the stage requirements file.")
    sub = p.add_subparsers(dest="cmd", required=True)

    # organize
    sp = sub.add_parser("organize", help="Flat CVAT export -> train/val + data.yaml")
    sp.add_argument("--src", default=str(RAW_EXPORT_DIR),
                    help="Flat export folder (or a parent of one). "
                         "Default: drop your export into raw_export/")
    sp.add_argument("--out", default=None, help="Output dataset dir (default datasets/<srcname>)")
    sp.add_argument("--train-ratio", type=float, default=0.85)
    sp.add_argument("--seed", type=int, default=42)
    sp.add_argument("--force", action="store_true", help="Replace existing --out")
    sp.set_defaults(func=cmd_organize)

    # train
    sp = sub.add_parser("train", help="Train YOLOv8n [TRAIN env]")
    sp.add_argument("--data", required=True, help="Path to data.yaml")
    sp.add_argument("--epochs", type=int, default=100)
    sp.add_argument("--batch", type=int, default=16)
    sp.add_argument("--img-height", type=int, default=480)
    sp.add_argument("--img-width", type=int, default=640)
    sp.add_argument("--project", default=str(HERE / "runs"))
    sp.add_argument("--name", default="yolov8n_k230d")
    sp.add_argument("--device", default="0", help="'0' for GPU, 'cpu' for CPU")
    sp.add_argument("--workers", type=int, default=8)
    sp.add_argument("--cache", action="store_true")
    sp.add_argument("--no-pretrained", action="store_true")
    sp.set_defaults(func=cmd_train)

    # export
    sp = sub.add_parser("export", help="best.pt -> ONNX [TRAIN env]")
    sp.add_argument("model", help="Path to trained .pt")
    sp.add_argument("--output", default=str(HERE / "models"))
    sp.add_argument("--img-height", type=int, default=480)
    sp.add_argument("--img-width", type=int, default=640)
    sp.add_argument("--opset", type=int, default=11)
    sp.add_argument("--no-simplify", action="store_true")
    sp.set_defaults(func=cmd_export)

    # shared convert args
    def add_convert_args(sp):
        sp.add_argument("onnx", help="Path to YOLOv8 .onnx")
        sp.add_argument("--calib-data", required=True, help="Calibration images dir (e.g. val/images)")
        sp.add_argument("--eval-data", default=None, help="Eval images dir for simulation (default: --calib-data)")
        sp.add_argument("--img-height", type=int, default=480)
        sp.add_argument("--img-width", type=int, default=640)
        sp.add_argument("--num-samples", type=int, default=8, help="Calibration images (Canaan default 8)")
        sp.add_argument("--eval-limit", type=int, default=20, help="Images used to simulate/score")
        sp.add_argument("--conf", type=float, default=0.05, help="Conf threshold for detection-delta scoring")
        sp.add_argument("--num-classes", type=int, default=None,
                        help="default: auto-detect from ONNX output (4+nc channels)")
        sp.add_argument("--target", default="k230")

    # convert-search
    sp = sub.add_parser("convert-search", help="Sweep PTQ options, simulate, pick best [CONVERT env]")
    add_convert_args(sp)
    sp.add_argument("--options", default=None,
                    help=f"Comma list of ptq options (default {','.join(map(str, DEFAULT_SEARCH_OPTIONS))})")
    sp.set_defaults(func=cmd_convert_search)

    # convert-preset
    sp = sub.add_parser("convert-preset", help="Compile predefined verified-best config [CONVERT env]")
    add_convert_args(sp)
    sp.set_defaults(func=cmd_convert_preset)

    # compare
    sp = sub.add_parser("compare", help="Compare searched vs preset [CONVERT env]")
    sp.set_defaults(func=cmd_compare)

    # rerank — re-select winner from existing sweep results (no recompile)
    sp = sub.add_parser("rerank", help="Re-pick winner from output/search/*/sim.json")
    sp.add_argument("--onnx", default=None)
    sp.add_argument("--img-height", type=int, default=480)
    sp.add_argument("--img-width", type=int, default=640)
    sp.add_argument("--num-samples", type=int, default=8)
    sp.add_argument("--target", default="k230")
    sp.set_defaults(func=cmd_rerank)

    # report
    sp = sub.add_parser("report", help="Generate HTML report from run artifacts")
    sp.add_argument("--data", default=str(HERE / "data.yaml"), help="data.yaml for dataset info")
    sp.add_argument("--output", default=None, help="HTML output (default report.html)")
    sp.set_defaults(func=cmd_report)

    # deploy
    sp = sub.add_parser("deploy", help="Bundle a model into deploy/ [CONVERT env]")
    sp.add_argument("--which", choices=["searched", "preset"], default="searched")
    sp.add_argument("--data", default=str(HERE / "data.yaml"), help="data.yaml for labels")
    sp.add_argument("--labels", nargs="+", default=None, help="Override labels (ordered by class id)")
    sp.add_argument("--img-height", type=int, default=480)
    sp.add_argument("--img-width", type=int, default=640)
    sp.add_argument("--conf", type=float, default=0.30)
    sp.add_argument("--iou", type=float, default=0.45)
    sp.set_defaults(func=cmd_deploy)

    # convert-all
    sp = sub.add_parser("convert-all", help="search + preset + compare + deploy [CONVERT env]")
    add_convert_args(sp)
    sp.add_argument("--options", default=None)
    sp.add_argument("--deploy-which", choices=["searched", "preset"], default="searched",
                    help="Which model to bundle into deploy/ at the end")
    # deploy needs these too
    sp.add_argument("--data", default=str(HERE / "data.yaml"))
    sp.add_argument("--labels", nargs="+", default=None)
    sp.add_argument("--deploy-conf", type=float, default=0.30, dest="conf2")
    sp.add_argument("--iou", type=float, default=0.45)
    sp.set_defaults(func=cmd_convert_all)
    return p


def main():
    args = build_parser().parse_args()
    # convert-all reuses cmd_deploy which reads args.conf (scoring conf) and
    # args.iou; keep the scoring conf for sim, deploy uses its own default.
    if getattr(args, "cmd", None) == "convert-all":
        args.which = args.deploy_which
    args.func(args)


if __name__ == "__main__":
    main()

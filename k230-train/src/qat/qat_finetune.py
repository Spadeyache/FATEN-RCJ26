"""QAT-assisted PTQ adaptation for the reconstructed AnchorBaseDet.

Goal: take the float32 PyTorch model whose direct uint8 PTQ collapses
to garbage, fine-tune it briefly with fake-quantization in the
forward path, then export to a regular float ONNX whose weights have
learned to be robust to uint8 quantization noise. nncase's PTQ on
that ONNX is then expected to retain accuracy.

Knowledge distillation from the float teacher avoids needing labels.
We just minimise MSE between teacher and student outputs on a
diverse batch of training images.

Run on Windows host (CPU is fine; the model is 1M params).
"""
from __future__ import annotations

import argparse
import copy
import glob
import os
import random
import sys
import time
from pathlib import Path

import numpy as np

try:
    import cv2
except ImportError as e:
    raise ImportError("opencv-python required") from e


def _force_cpu_torch():
    import torch.serialization as ts
    ts.default_restore_location = lambda storage, _loc: storage
    if hasattr(ts, "_get_restore_location"):
        ts._get_restore_location = lambda *a, **kw: (lambda storage, _loc: storage)


sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))

import torch  # noqa: E402
import torch.nn as nn  # noqa: E402
_force_cpu_torch()

from reconstruct.build_anchorbasedet import AnchorBaseDet  # noqa: E402
from common.letterbox import letterbox_bgr  # noqa: E402


def list_images(directory: str):
    files = []
    for ext in ("*.jpg", "*.jpeg", "*.png", "*.bmp"):
        files.extend(glob.glob(os.path.join(directory, ext)))
        files.extend(glob.glob(os.path.join(directory, ext.upper())))
    return sorted(files)


def build_input_batch(images_dir: str, count: int, width: int, height: int,
                     mean, std, use_letterbox: bool, seed: int = 42) -> torch.Tensor:
    """Return float32 NCHW batch already ImageNet-normalized, ready for forward."""
    files = list_images(images_dir)
    if not files:
        raise FileNotFoundError(images_dir)
    # Filter out empty/unreadable files first.
    good = []
    for fp in files:
        if os.path.getsize(fp) == 0:
            continue
        good.append(fp)
    files = good
    if not files:
        raise FileNotFoundError(f"No readable images in {directory}")

    rng = random.Random(seed)
    if len(files) > count:
        files = rng.sample(files, count)
    else:
        while len(files) < count:
            files.append(rng.choice(files))
        files = files[:count]

    mean_t = np.array(mean, dtype=np.float32).reshape(1, 3, 1, 1)
    std_t = np.array(std, dtype=np.float32).reshape(1, 3, 1, 1)

    batch = np.zeros((count, 3, height, width), dtype=np.float32)
    for i, fp in enumerate(files):
        bgr = cv2.imread(fp, cv2.IMREAD_COLOR)
        if bgr is None:
            print(f"WARN: skipping unreadable {fp}; using zero image")
            continue
        if use_letterbox:
            letter, _, _ = letterbox_bgr(bgr, width, height)
        else:
            letter = cv2.resize(bgr, (width, height),
                                interpolation=cv2.INTER_LINEAR)
        rgb = cv2.cvtColor(letter, cv2.COLOR_BGR2RGB)
        chw = rgb.transpose(2, 0, 1).astype(np.float32) / 255.0
        batch[i] = chw
    batch = (batch - mean_t) / std_t
    return torch.from_numpy(batch)


# ----------------------------------------------------------------------------
# Lightweight fake-quant insertion. We replace nn.Conv2d (incl. depthwise)
# with a wrapper that fake-quants its weights symmetrically per-channel
# (int8) and fake-quants its output activations per-tensor (uint8). BN /
# SE are left in float because nncase will fuse / handle those during its
# own PTQ pass on the exported ONNX.
# ----------------------------------------------------------------------------


def _round_ste(x: torch.Tensor) -> torch.Tensor:
    """Round with straight-through estimator gradient."""
    return (torch.round(x) - x).detach() + x


def fake_quantize_weight(w: torch.Tensor, num_bits: int = 8) -> torch.Tensor:
    """Per-output-channel symmetric fake quantization with STE.

    Without STE the gradient through `torch.round` is zero and the wrapped
    Conv weights effectively don't train. We use _round_ste so backprop
    sees the quant as an identity on gradients (standard QAT trick).
    """
    qmin = -(2 ** (num_bits - 1))
    qmax = (2 ** (num_bits - 1)) - 1
    flat = w.reshape(w.shape[0], -1)
    max_abs = flat.abs().amax(dim=1).clamp_min(1e-8)
    scale = max_abs / qmax
    scale = scale.reshape(-1, *([1] * (w.dim() - 1)))
    q = torch.clamp(_round_ste(w / scale), qmin, qmax)
    return q * scale


class _ActFakeQuant(nn.Module):
    """Per-tensor uint8 fake-quant on activations, with EMA range tracking."""

    def __init__(self, ema_momentum: float = 0.99):
        super().__init__()
        self.register_buffer("min_val", torch.tensor(0.0))
        self.register_buffer("max_val", torch.tensor(0.0))
        self.register_buffer("initialised", torch.tensor(0.0))
        self.ema_momentum = ema_momentum

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        if self.training:
            cur_min = x.detach().min()
            cur_max = x.detach().max()
            if self.initialised.item() == 0.0:
                self.min_val.copy_(cur_min)
                self.max_val.copy_(cur_max)
                self.initialised.fill_(1.0)
            else:
                m = self.ema_momentum
                self.min_val.copy_(m * self.min_val + (1 - m) * cur_min)
                self.max_val.copy_(m * self.max_val + (1 - m) * cur_max)
        if self.initialised.item() == 0.0:
            return x
        mn, mx = self.min_val, self.max_val
        scale = (mx - mn).clamp_min(1e-8) / 255.0
        zp = torch.round(-mn / scale)
        # Asymmetric uint8 fake-quant with STE on the round.
        q = torch.clamp(_round_ste(x / scale + zp), 0, 255)
        return (q - zp) * scale


class _ConvFakeQuantWrapper(nn.Module):
    """Wraps an nn.Conv2d, fake-quanting weights and post-output activations."""

    def __init__(self, conv: nn.Conv2d):
        super().__init__()
        self.conv = conv
        self.act_fq = _ActFakeQuant()

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        w_fq = fake_quantize_weight(self.conv.weight) if self.training else self.conv.weight
        out = nn.functional.conv2d(
            x, w_fq, self.conv.bias,
            self.conv.stride, self.conv.padding,
            self.conv.dilation, self.conv.groups,
        )
        return self.act_fq(out)


def wrap_convs_with_fakequant(module: nn.Module):
    """Replace each nn.Conv2d in `module` with _ConvFakeQuantWrapper."""
    for name, child in list(module.named_children()):
        if isinstance(child, nn.Conv2d):
            setattr(module, name, _ConvFakeQuantWrapper(child))
        else:
            wrap_convs_with_fakequant(child)


def unwrap_fakequant(module: nn.Module, bake_quant: bool = True):
    """Inverse: pull the wrapped Conv2d back out so ONNX export is clean.

    If `bake_quant=True`, the conv's float weights are REPLACED with
    their fake-quantized values (still stored as float32, but values are
    on the int8 grid). This way the float ONNX matches what the QAT
    student saw during training, and nncase's PTQ will trivially
    re-quantize the same way.

    Without baking, the float weights drifted during training to
    compensate for FQ noise that's no longer present after unwrap,
    which destroys the model -- we saw this experimentally.
    """
    for name, child in list(module.named_children()):
        if isinstance(child, _ConvFakeQuantWrapper):
            conv = child.conv
            if bake_quant:
                with torch.no_grad():
                    conv.weight.copy_(fake_quantize_weight(conv.weight))
            setattr(module, name, conv)
        else:
            unwrap_fakequant(child, bake_quant=bake_quant)


# ----------------------------------------------------------------------------
# Main
# ----------------------------------------------------------------------------


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pt", default="data/anchorbasedet_reconstructed.pt")
    ap.add_argument("--images-dir",
                    default="../k230-training/datasets/my_dataset/train/images")
    ap.add_argument("--out-onnx", default="data/anchorbasedet_qat.onnx")
    ap.add_argument("--out-pt", default="data/anchorbasedet_qat.pt")
    ap.add_argument("--input-width", type=int, default=640)
    ap.add_argument("--input-height", type=int, default=480)
    ap.add_argument("--num-images", type=int, default=64)
    ap.add_argument("--batch-size", type=int, default=4)
    ap.add_argument("--num-steps", type=int, default=400)
    ap.add_argument("--lr", type=float, default=1e-4)
    ap.add_argument("--use-letterbox", action="store_true", default=True)
    ap.add_argument("--no-letterbox", dest="use_letterbox",
                    action="store_false")
    ap.add_argument("--seed", type=int, default=42)
    args = ap.parse_args()

    torch.manual_seed(args.seed)
    random.seed(args.seed)
    np.random.seed(args.seed)

    # Load float teacher.
    blob = torch.load(args.pt, map_location="cpu", weights_only=False)
    teacher = AnchorBaseDet(num_classes=int(blob["cfg"]["num_classes"]))
    teacher.load_state_dict(blob["model_state_dict"], strict=True)
    teacher.eval()
    for p in teacher.parameters():
        p.requires_grad_(False)
    mean = blob["mean"]
    std = blob["std"]
    print(f"Loaded teacher from {args.pt}: mean={mean}, std={std}")

    # Build student with fake-quant inserts.
    student = copy.deepcopy(teacher)
    # deepcopy inherits requires_grad=False from the frozen teacher.
    for p in student.parameters():
        p.requires_grad_(True)
    wrap_convs_with_fakequant(student)
    student.train()
    n_params = sum(p.numel() for p in student.parameters() if p.requires_grad)
    print(f"Student has {n_params:,} trainable params with fake-quant on every Conv2d")

    # Load training batch (one shot; we just sample mini-batches from it).
    print(f"Loading {args.num_images} images from {args.images_dir} "
          f"(letterbox={args.use_letterbox}) ...")
    full_batch = build_input_batch(
        args.images_dir, args.num_images,
        args.input_width, args.input_height,
        mean, std, use_letterbox=args.use_letterbox,
    )
    print(f"Batch shape: {tuple(full_batch.shape)}")

    # Warm-up in train mode so the activation observers learn ranges.
    student.train()
    with torch.no_grad():
        for i in range(0, args.num_images, args.batch_size):
            x = full_batch[i:i + args.batch_size]
            student(x)
    print("Observers seeded.")

    # Sanity check: student forward (with fake-quant active) vs teacher.
    # We DO want a non-zero diff here -- that's evidence the fake-quants
    # are perturbing the activations as intended. The training loop will
    # then learn weights that compensate.
    student.train()
    with torch.no_grad():
        t_outs = teacher(full_batch[:args.batch_size])
        s_outs = student(full_batch[:args.batch_size])
    pre_diff = sum(float((s - t).abs().mean()) for s, t in zip(s_outs, t_outs))
    print(f"Pre-training mean abs diff (teacher vs FQ student): {pre_diff:.5f}")
    if pre_diff < 1e-6:
        print("WARN: fake-quant produces zero diff -- weight FQ may not be active.")

    student.train()
    optim = torch.optim.AdamW(
        [p for p in student.parameters() if p.requires_grad],
        lr=args.lr, weight_decay=1e-4,
    )
    mse = nn.MSELoss()

    t0 = time.time()
    losses = []
    for step in range(args.num_steps):
        idx = torch.randint(0, args.num_images, (args.batch_size,))
        x = full_batch[idx]
        with torch.no_grad():
            t_outs = teacher(x)
        s_outs = student(x)
        loss = sum(mse(s, t) for s, t in zip(s_outs, t_outs))
        optim.zero_grad()
        loss.backward()
        optim.step()
        losses.append(float(loss.item()))
        if step % 25 == 0 or step == args.num_steps - 1:
            elapsed = time.time() - t0
            print(f"step {step:4d}/{args.num_steps}  "
                  f"loss={loss.item():.6f}  elapsed={elapsed:.1f}s")
    print(f"Training done in {time.time() - t0:.1f}s")

    # Switch student to eval (deterministic for export).
    student.eval()

    # Post-training sanity check.
    with torch.no_grad():
        s_outs = student(full_batch[:args.batch_size])
    post_diff = sum(float((s - t).abs().mean()) for s, t in zip(s_outs, t_outs))
    print(f"Post-training mean abs diff (teacher vs student in eval w/ FQ): "
          f"{post_diff:.5f}")

    # Save student .pt (with wrapped Convs) for inspection.
    torch.save({
        "model_state_dict": student.state_dict(),
        "cfg": blob["cfg"],
        "mean": mean,
        "std": std,
        "qat_num_steps": args.num_steps,
        "qat_batch_size": args.batch_size,
        "qat_num_images": args.num_images,
    }, args.out_pt)
    print(f"Saved QAT student to {args.out_pt}")

    # Strip fake-quants and export a plain float ONNX.
    unwrap_fakequant(student)
    dummy = torch.zeros(1, 3, args.input_height, args.input_width)
    with torch.no_grad():
        outs = student(dummy)
    print(f"Post-strip output shapes: {[tuple(o.shape) for o in outs]}")

    torch.onnx.export(
        student, dummy, args.out_onnx,
        input_names=["input"],
        output_names=["out0_s8", "out1_s16", "out2_s32"],
        opset_version=11, do_constant_folding=True,
    )
    print(f"Exported QAT-adapted ONNX -> {args.out_onnx} "
          f"({os.path.getsize(args.out_onnx):,} bytes)")

    # Optional: simplify
    try:
        import onnx
        import onnxsim
        m = onnx.load(args.out_onnx)
        s, ok = onnxsim.simplify(m)
        assert ok
        onnx.save(s, args.out_onnx)
        print(f"onnxsim -> {os.path.getsize(args.out_onnx):,} bytes")
    except Exception as e:
        print(f"WARN: onnxsim failed: {e}")


if __name__ == "__main__":
    main()

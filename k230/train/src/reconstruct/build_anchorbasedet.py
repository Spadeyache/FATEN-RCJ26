"""Reconstruct AI Cube's `AnchorBaseDet` (can3_5_s) model in PyTorch.

Architecture pieces (verified from state_dict shapes):

- backbone: MobileNetV3-Small-like with width_mult=0.5 and Canaan's "can3"
  channel widths. Returns features at strides 8, 16, 32.
- adapt_layers: three 1x1 ConvBN that unify the backbone outputs to
  64 / 128 / 256 channels (lazy "lateral" layers).
- neck: an FPN top-down at stride 16 only
    P4_1 = C3(concat(Upsample(A5), A4))   384 -> 128 ch, stride 16
    P4_2 = ConvBN1x1                      128 -> 64 ch, stride 16
- pan: bottom-up PAN
    P3 = C3(concat(Upsample(P4_2), A3))    128 -> 64 ch, stride 8 -> head[0]
    P4 = C3(concat(convP3(P3), P4_2))      128 -> 128 ch, stride 16 -> head[1]
    P5 = C3(A5)                            256 -> 256 ch, stride 32 -> head[2]
  (convP4 is present in the state_dict but is not used in the forward path
   based on channel-shape analysis -- it stays as a buffer of unused weights.)
- head: YOLOv5 Detect-style
    m.{0,1,2} = 1x1 Conv  ch -> 21
    Sigmoid applied; output permuted to NHWC (1, H, W, 21)

This file just defines the modules. Weight loading is in load_from_npy.py.
"""
from __future__ import annotations

from collections import OrderedDict
from dataclasses import dataclass
from typing import List, Tuple

import torch
import torch.nn as nn


# ----------------------------------------------------------------------------
# Building blocks
# ----------------------------------------------------------------------------


def autopad(k, p=None):
    if p is None:
        return k // 2 if isinstance(k, int) else [x // 2 for x in k]
    return p


def _make_divisible(v: float, divisor: int = 8, min_value=None) -> int:
    """torchvision's MobileNet rounding helper for SE squeeze widths."""
    if min_value is None:
        min_value = divisor
    new_v = max(min_value, int(v + divisor / 2) // divisor * divisor)
    if new_v < 0.9 * v:
        new_v += divisor
    return new_v


def _make_act(name: str) -> nn.Module:
    if name == "relu":
        return nn.ReLU(inplace=True)
    if name == "hardswish":
        return nn.Hardswish(inplace=True)
    if name == "identity":
        return nn.Identity()
    raise ValueError(f"Unknown activation: {name}")


class ConvBNAct(nn.Sequential):
    """Sequential Conv -> BN -> Act so saved keys are 0.weight, 1.weight, etc.

    This matches torchvision's `Conv2dNormActivation` exactly.
    """

    def __init__(self, in_ch, out_ch, k=1, s=1, p=None, g=1,
                 act: str = "hardswish", bias: bool = False):
        layers = [
            nn.Conv2d(in_ch, out_ch, k, s, autopad(k, p),
                      groups=g, bias=bias),
            nn.BatchNorm2d(out_ch),
            _make_act(act),
        ]
        super().__init__(*layers)


class SqueezeExcitation(nn.Module):
    """Torchvision-compatible SE block.

    Layout matches `block.{N}.fc1`, `fc2`.
    """

    def __init__(self, in_ch: int, squeeze_ch: int):
        super().__init__()
        self.fc1 = nn.Conv2d(in_ch, squeeze_ch, 1, bias=True)
        self.fc2 = nn.Conv2d(squeeze_ch, in_ch, 1, bias=True)

    def forward(self, x):
        scale = nn.functional.adaptive_avg_pool2d(x, 1)
        scale = self.fc1(scale)
        scale = nn.functional.relu(scale, inplace=True)
        scale = self.fc2(scale)
        scale = nn.functional.hardsigmoid(scale, inplace=True)
        return x * scale


class InvertedResidualBlock(nn.Module):
    """Mobilenet-V3-style block matching the state-dict layout used here.

    Two configurations occur in the model:

      (1) First block (features.1): no expand, has SE.
          block.0 = depthwise ConvBN
          block.1 = SE
          block.2 = project ConvBN1x1
      (2) Standard blocks (features.2+): expand + depthwise + (optional SE) +
          project. With SE, block.2 is SE and block.3 is project.

    We don't track these via `nn.Sequential` ordering. Instead we just store
    the modules under matching names so `load_state_dict(strict=True)` works.
    """

    def __init__(self, in_ch: int, exp_ch: int, out_ch: int, kernel: int,
                 stride: int, use_se: bool, act: str = "hardswish",
                 squeeze_factor: int = 4, has_expand: bool = True):
        super().__init__()
        self.in_ch = in_ch
        self.out_ch = out_ch
        self.stride = stride
        self.use_se = use_se
        self.use_residual = stride == 1 and in_ch == out_ch

        layers = OrderedDict()
        idx = 0
        if has_expand:
            layers[str(idx)] = ConvBNAct(in_ch, exp_ch, k=1, s=1, act=act)
            idx += 1
        # depthwise
        layers[str(idx)] = ConvBNAct(exp_ch, exp_ch, k=kernel, s=stride,
                                     g=exp_ch, act=act)
        idx += 1
        if use_se:
            squeeze = _make_divisible(exp_ch // squeeze_factor, 8)
            layers[str(idx)] = SqueezeExcitation(exp_ch, squeeze)
            idx += 1
        # project
        layers[str(idx)] = ConvBNAct(exp_ch, out_ch, k=1, s=1, act="identity")
        idx += 1
        self.block = nn.ModuleDict(layers)
        self._idx = idx

    def forward(self, x):
        identity = x
        out = x
        for k in self.block:
            out = self.block[k](out)
        if self.use_residual:
            return out + identity
        return out


# ----------------------------------------------------------------------------
# can3 backbone (per state_dict)
# ----------------------------------------------------------------------------
# Each tuple: (kernel, expand_ch, out_ch, use_se, act, stride)
# Derived from the dump: backbone.model.features.{N}.block.* shapes.
# features.0: stem Conv (3 -> 8, k=3, s=2, hardswish)
# features.1: dw + SE + project (8 -> 8, k=3, s=2, has_expand=False)
# features.2: 1x1(8->40) + dw3x3 s=2 + project(40->16) -- no SE
# features.3: 1x1(16->48) + dw3x3 s=1 + project(48->16) -- no SE
# features.4: 1x1(16->48) + dw5x5 s=2 + SE + project(48->24)
# features.5: 1x1(24->120) + dw5x5 s=1 + SE + project(120->24)
# features.6: 1x1(24->120) + dw5x5 s=1 + SE + project(120->24)
# features.7: 1x1(24->64) + dw5x5 s=1 + SE + project(64->24)
# features.8: 1x1(24->...) + dw + SE + project(->24)
# features.9: 1x1(24->144) + dw5x5 s=2 + SE + project(144->48)
# features.10/11: 1x1(48->288) + dw5x5 s=1 + SE + project(288->48)
#
# Stride accumulation: stem(s=2) -> f.1(s=2)=stride 4 -> f.2(s=2)=stride 8
# -> f.4(s=2)=stride 16 -> f.9(s=2)=stride 32.
# Outputs:
#   stride 8  = features.3 (16 ch)
#   stride 16 = features.8 (24 ch)
#   stride 32 = features.11 (48 ch)

CAN3_CONFIG = [
    # kernel, exp, out, use_se, act, stride, has_expand
    (3,   8,   8,  True,  "relu",      2, False),  # f.1
    (3,  40,  16,  False, "relu",      2, True),   # f.2
    (3,  48,  16,  False, "relu",      1, True),   # f.3   <- stride 8 output
    (5,  48,  24,  True,  "hardswish", 2, True),   # f.4
    (5, 120,  24,  True,  "hardswish", 1, True),   # f.5
    (5, 120,  24,  True,  "hardswish", 1, True),   # f.6
    (5,  64,  24,  True,  "hardswish", 1, True),   # f.7
    (5,  72,  24,  True,  "hardswish", 1, True),   # f.8   <- stride 16 output
    (5, 144,  48,  True,  "hardswish", 2, True),   # f.9
    (5, 288,  48,  True,  "hardswish", 1, True),   # f.10
    (5, 288,  48,  True,  "hardswish", 1, True),   # f.11  <- stride 32 output
]


class Can3Backbone(nn.Module):
    """Mobilenet-V3-like backbone with the channel widths from the .npy.

    Returns features at strides 8, 16, 32.
    """

    OUT_STRIDES = {3: 8, 8: 16, 11: 32}

    def __init__(self):
        super().__init__()
        self.model = nn.Module()
        self.model.features = nn.Sequential()

        # features.0 — stem: Conv(3->8, k=3, s=2) + BN + Hardswish
        self.model.features.append(ConvBNAct(3, 8, k=3, s=2, act="hardswish"))

        in_ch = 8
        for i, (k, exp, out, se, act, s, has_exp) in enumerate(CAN3_CONFIG):
            self.model.features.append(InvertedResidualBlock(
                in_ch=in_ch, exp_ch=exp, out_ch=out, kernel=k, stride=s,
                use_se=se, act=act, has_expand=has_exp,
            ))
            in_ch = out

    def forward(self, x):
        out_strides = {}
        for idx, m in enumerate(self.model.features):
            x = m(x)
            if idx in self.OUT_STRIDES:
                out_strides[self.OUT_STRIDES[idx]] = x
        return out_strides[8], out_strides[16], out_strides[32]


# ----------------------------------------------------------------------------
# YOLOv5-style C3 + ConvBN (used by neck/pan)
# ----------------------------------------------------------------------------


class ConvBNSilu(nn.Module):
    """Standard Conv + BN + SiLU used in YOLOv5 necks (matches *.conv + *.bn)."""

    def __init__(self, in_ch, out_ch, k=1, s=1, p=None, g=1):
        super().__init__()
        self.conv = nn.Conv2d(in_ch, out_ch, k, s, autopad(k, p),
                              groups=g, bias=False)
        self.bn = nn.BatchNorm2d(out_ch)
        self.act = nn.SiLU(inplace=True)

    def forward(self, x):
        return self.act(self.bn(self.conv(x)))


class Bottleneck(nn.Module):
    """YOLOv5 bottleneck: cv1 (1x1) + cv2 (3x3), optional residual."""

    def __init__(self, c1, c2, shortcut=True, g=1, e=1.0):
        super().__init__()
        c_ = int(c2 * e)
        self.cv1 = ConvBNSilu(c1, c_, 1, 1)
        self.cv2 = ConvBNSilu(c_, c2, 3, 1, g=g)
        self.add = shortcut and c1 == c2

    def forward(self, x):
        return x + self.cv2(self.cv1(x)) if self.add else self.cv2(self.cv1(x))


class C3(nn.Module):
    """YOLOv5 C3 module: cv1 + cv2 (parallel) + bottlenecks(m) + cv3."""

    def __init__(self, c1, c2, n=1, shortcut=True, g=1, e=0.5):
        super().__init__()
        c_ = int(c2 * e)
        self.cv1 = ConvBNSilu(c1, c_, 1, 1)
        self.cv2 = ConvBNSilu(c1, c_, 1, 1)
        self.cv3 = ConvBNSilu(2 * c_, c2, 1, 1)
        self.m = nn.Sequential(*(Bottleneck(c_, c_, shortcut, g, e=1.0)
                                  for _ in range(n)))

    def forward(self, x):
        return self.cv3(torch.cat((self.m(self.cv1(x)), self.cv2(x)), dim=1))


# ----------------------------------------------------------------------------
# Adapt / Neck / Pan / Head
# ----------------------------------------------------------------------------


class AdaptLayer(nn.Module):
    """Single Conv1x1+BN with bias-on conv, identity activation.

    Matches `adapt_layers.layer{N}.conv` and `.bn`.
    """

    def __init__(self, in_ch: int, out_ch: int):
        super().__init__()
        self.conv = nn.Conv2d(in_ch, out_ch, 1, 1, 0, bias=True)
        self.bn = nn.BatchNorm2d(out_ch)

    def forward(self, x):
        return self.bn(self.conv(x))


class AdaptLayers(nn.Module):
    def __init__(self):
        super().__init__()
        self.layer0 = AdaptLayer(16, 64)
        self.layer1 = AdaptLayer(24, 128)
        self.layer2 = AdaptLayer(48, 256)

    def forward(self, f3, f4, f5):
        return self.layer0(f3), self.layer1(f4), self.layer2(f5)


class Neck(nn.Module):
    """FPN top-down at stride 16."""

    def __init__(self):
        super().__init__()
        self.P4_1 = C3(384, 128, n=1, shortcut=False)
        self.P4_2 = ConvBNSilu(128, 64, 1, 1)

    def forward(self, a4, a5):
        a5_up = nn.functional.interpolate(a5, scale_factor=2.0, mode="nearest")
        x = torch.cat([a5_up, a4], dim=1)         # 256+128=384, stride 16
        x = self.P4_1(x)                           # 128, stride 16
        return self.P4_2(x), x                     # P4_2 (64), P4_1 (128)


class Pan(nn.Module):
    """Bottom-up PAN."""

    def __init__(self):
        super().__init__()
        self.P3 = C3(128, 64, n=1, shortcut=False)
        self.convP3 = ConvBNSilu(64, 64, 3, 2)

        self.P4 = C3(128, 128, n=1, shortcut=False)
        self.convP4 = ConvBNSilu(128, 128, 3, 2)

        # pan.P5 takes Concat(convP4(P4), A5) -- 128 + 256 = 384 input.
        self.P5 = C3(384, 256, n=1, shortcut=False)

    def forward(self, a3, p4_2, a5):
        p4_2_up = nn.functional.interpolate(p4_2, scale_factor=2.0,
                                            mode="nearest")
        x3_in = torch.cat([p4_2_up, a3], dim=1)        # 64+64=128, stride 8
        p3_out = self.P3(x3_in)                          # 64, stride 8

        p3_down = self.convP3(p3_out)                    # 64, stride 16
        x4_in = torch.cat([p3_down, p4_2], dim=1)        # 64+64=128, stride 16
        p4_out = self.P4(x4_in)                          # 128, stride 16

        p4_down = self.convP4(p4_out)                    # 128, stride 32
        x5_in = torch.cat([p4_down, a5], dim=1)          # 128+256=384, stride 32
        p5_out = self.P5(x5_in)                          # 256, stride 32

        return p3_out, p4_out, p5_out


class DetectHead(nn.Module):
    """YOLOv5 Detect-style head with sigmoid + NHWC reshape.

    Anchor buffers `anchors` (3,3,2) and `anchor_grid` (3,1,3,1,1,2) are
    stored as registered buffers so the state_dict matches strictly.
    """

    def __init__(self, num_classes: int = 2, in_channels=(64, 128, 256),
                 anchors_per_scale: List[List[Tuple[float, float]]] = None):
        super().__init__()
        self.nc = num_classes
        self.no = num_classes + 5
        self.na = 3
        self.nl = 3

        self.m = nn.ModuleList([
            nn.Conv2d(c, self.na * self.no, 1, 1, bias=True)
            for c in in_channels
        ])
        # Register anchor buffers; values are loaded from state_dict.
        self.register_buffer("anchors", torch.zeros(self.nl, self.na, 2))
        self.register_buffer("anchor_grid",
                             torch.zeros(self.nl, 1, self.na, 1, 1, 2))

    def forward(self, feats):
        outputs = []
        for i, f in enumerate(feats):
            raw = self.m[i](f)                           # (B, na*no, H, W)
            raw = raw.sigmoid()
            # Permute to (B, H, W, na*no) NHWC for the K230 deploy decoder.
            out = raw.permute(0, 2, 3, 1).contiguous()
            outputs.append(out)
        return outputs


# ----------------------------------------------------------------------------
# Full model
# ----------------------------------------------------------------------------


class AnchorBaseDet(nn.Module):
    def __init__(self, num_classes: int = 2):
        super().__init__()
        self.backbone = Can3Backbone()
        self.adapt_layers = AdaptLayers()
        self.neck = Neck()
        self.pan = Pan()
        self.head = DetectHead(num_classes=num_classes)

    def forward(self, x):
        f3, f4, f5 = self.backbone(x)
        a3, a4, a5 = self.adapt_layers(f3, f4, f5)
        p4_2, _p4_1 = self.neck(a4, a5)
        p3, p4, p5 = self.pan(a3, p4_2, a5)
        return self.head([p3, p4, p5])


def main():
    import argparse
    p = argparse.ArgumentParser()
    p.add_argument("--num-classes", type=int, default=2)
    p.add_argument("--input-shape", default="1,3,480,640")
    p.add_argument("--summary", action="store_true",
                   help="Print a parameter count summary")
    args = p.parse_args()

    model = AnchorBaseDet(num_classes=args.num_classes).eval()
    shape = tuple(int(x) for x in args.input_shape.split(","))
    x = torch.zeros(shape, dtype=torch.float32)
    with torch.no_grad():
        outs = model(x)
    print("Reconstructed AnchorBaseDet")
    for i, o in enumerate(outs):
        print(f"  out[{i}]: shape={tuple(o.shape)} dtype={o.dtype}")
    if args.summary:
        n = sum(p.numel() for p in model.parameters())
        print(f"  total params: {n:,}")


if __name__ == "__main__":
    main()

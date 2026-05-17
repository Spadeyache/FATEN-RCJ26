"""Typed loader/saver for deploy_config.json.

Validates that the config is a valid AnchorBaseDet config (3 anchors per
scale, num_classes matching categories). Resolves kmodel_path relative to
the config directory so we can keep configs portable.
"""
from __future__ import annotations

from dataclasses import dataclass, field, asdict
import json
import os
from typing import List, Optional


@dataclass
class DeployConfig:
    nncase_version: str
    chip_type: str
    inference_width: int
    inference_height: int
    confidence_threshold: float
    nms_threshold: float
    calibrate_method: str
    ptq_option: str
    model_type: str
    img_size: List[int]
    anchors: List[List[float]]              # 3 scales x [w0,h0,w1,h1,w2,h2]
    mean: List[float]
    std: List[float]
    categories: List[str]
    nms_option: bool
    kmodel_path: str
    num_classes: int
    raw: dict = field(default_factory=dict)

    @property
    def anchors_per_scale(self):
        out = []
        for scale in self.anchors:
            out.append([
                (float(scale[0]), float(scale[1])),
                (float(scale[2]), float(scale[3])),
                (float(scale[4]), float(scale[5])),
            ])
        return out

    @property
    def width(self) -> int:
        return int(self.img_size[0])

    @property
    def height(self) -> int:
        return int(self.img_size[1])


def load_deploy_config(path: str) -> DeployConfig:
    with open(path, "r", encoding="utf-8") as f:
        raw = json.load(f)

    if raw.get("model_type") != "AnchorBaseDet":
        raise ValueError(
            f"This pipeline supports AnchorBaseDet only, got "
            f"{raw.get('model_type')!r} in {path}"
        )

    anchors = raw["anchors"]
    if len(anchors) != 3:
        raise ValueError(f"Expected 3 anchor scales, got {len(anchors)}")
    for i, scale in enumerate(anchors):
        if len(scale) != 6:
            raise ValueError(
                f"anchors[{i}] should be 6 values (3 anchors of w,h); "
                f"got {len(scale)}"
            )

    if len(raw["categories"]) != raw["num_classes"]:
        raise ValueError(
            f"num_classes={raw['num_classes']} but categories has "
            f"{len(raw['categories'])} entries"
        )

    cfg = DeployConfig(
        nncase_version=raw["nncase_version"],
        chip_type=raw["chip_type"],
        inference_width=raw["inference_width"],
        inference_height=raw["inference_height"],
        confidence_threshold=float(raw["confidence_threshold"]),
        nms_threshold=float(raw["nms_threshold"]),
        calibrate_method=raw["calibrate_method"],
        ptq_option=raw["ptq_option"],
        model_type=raw["model_type"],
        img_size=list(raw["img_size"]),
        anchors=anchors,
        mean=list(raw["mean"]),
        std=list(raw["std"]),
        categories=list(raw["categories"]),
        nms_option=bool(raw["nms_option"]),
        kmodel_path=raw["kmodel_path"],
        num_classes=int(raw["num_classes"]),
        raw=raw,
    )

    if not os.path.isabs(cfg.kmodel_path):
        cfg.kmodel_path = os.path.join(os.path.dirname(path), cfg.kmodel_path)
    return cfg


def save_deploy_config(path: str, cfg: DeployConfig,
                       extra_meta: Optional[dict] = None) -> None:
    out = {
        "nncase_version": cfg.nncase_version,
        "chip_type": cfg.chip_type,
        "inference_width": cfg.inference_width,
        "inference_height": cfg.inference_height,
        "confidence_threshold": cfg.confidence_threshold,
        "nms_threshold": cfg.nms_threshold,
        "calibrate_method": cfg.calibrate_method,
        "ptq_option": cfg.ptq_option,
        "model_type": cfg.model_type,
        "img_size": cfg.img_size,
        "anchors": cfg.anchors,
        "mean": cfg.mean,
        "std": cfg.std,
        "categories": cfg.categories,
        "nms_option": cfg.nms_option,
        "kmodel_path": cfg.kmodel_path if os.path.isabs(cfg.kmodel_path)
                       else os.path.basename(cfg.kmodel_path),
        "num_classes": cfg.num_classes,
    }
    if extra_meta:
        out["_meta"] = extra_meta
    with open(path, "w", encoding="utf-8") as f:
        json.dump(out, f, indent=4)

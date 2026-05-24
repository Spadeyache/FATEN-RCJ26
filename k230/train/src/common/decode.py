"""AnchorBaseDet decoder matching AI Cube C++ deployment.

Output tensor layout (per scale): (1, H, W, 21) with 21 = 3 anchors * (5 + 2 classes).
The trailing 21 unpacks as 3 records of [x, y, w, h, obj, cls0, cls1].

Sigmoid is BAKED into the model head, so the values we read here are already
in [0, 1]. The decoder formulas are:

    cx = ((x * 2) - 0.5 + grid_x) * stride
    cy = ((y * 2) - 0.5 + grid_y) * stride
    bw = ((w * 2) ** 2) * anchor_w
    bh = ((h * 2) ** 2) * anchor_h
    score = obj * max(cls_scores)

These coordinates are in the model input frame (e.g. 640x480 letterboxed).
Use `letterbox.inverse_transform_box` to map back to original image space.
"""
from __future__ import annotations

from dataclasses import dataclass
import numpy as np

STRIDES = (8, 16, 32)


@dataclass
class DecodedBox:
    cls: int
    label: str
    score: float
    obj: float
    cls_score: float
    x1: float
    y1: float
    x2: float
    y2: float
    scale: int
    gy: int
    gx: int
    anchor: int

    @property
    def box(self):
        return [self.x1, self.y1, self.x2, self.y2]


def _decode_one(rec, gx: int, gy: int, anchor_w: float, anchor_h: float,
                stride: int):
    x = float(rec[0])
    y = float(rec[1])
    w = float(rec[2])
    h = float(rec[3])

    cx = ((x * 2.0) - 0.5 + gx) * stride
    cy = ((y * 2.0) - 0.5 + gy) * stride
    bw = ((w * 2.0) ** 2) * anchor_w
    bh = ((h * 2.0) ** 2) * anchor_h

    return (cx - bw / 2.0, cy - bh / 2.0, cx + bw / 2.0, cy + bh / 2.0)


def decode_outputs(outputs, anchors_per_scale, labels, num_classes,
                   strides=STRIDES, score_threshold: float = 0.0):
    """Decode the three output tensors.

    outputs:           list of 3 ndarrays, each (1, H, W, 3*(5+C)) [NHWC layout]
    anchors_per_scale: list of 3 lists of (anchor_w, anchor_h) tuples
    labels:            list of class names
    num_classes:       int
    strides:           tuple of 3 strides
    score_threshold:   skip boxes below this for speed

    Returns: list[DecodedBox].
    """
    one_record = 5 + num_classes
    expected_last = 3 * one_record
    boxes = []

    for scale_i, out in enumerate(outputs):
        if out.shape[-1] != expected_last:
            raise ValueError(
                f"Output[{scale_i}] last dim={out.shape[-1]} != "
                f"3*(5+{num_classes})={expected_last}. "
                f"Check num_classes/config or model output layout."
            )

        h_grid = out.shape[1]
        w_grid = out.shape[2]
        stride = strides[scale_i]
        anchors = anchors_per_scale[scale_i]
        y_grid = out[0].reshape(h_grid, w_grid, 3, one_record)

        # Vectorised score gate before the per-cell loop.
        obj_all = y_grid[..., 4]
        cls_all = y_grid[..., 5:5 + num_classes]
        score_all = obj_all[..., None] * cls_all  # (H, W, 3, C)
        mask = score_all >= score_threshold

        if not mask.any():
            continue

        # Iterate only over (gy, gx, anchor, cls) cells passing the gate.
        idx = np.argwhere(mask)
        for gy, gx, ai, cls in idx:
            obj = float(obj_all[gy, gx, ai])
            cls_score = float(cls_all[gy, gx, ai, cls])
            score = float(score_all[gy, gx, ai, cls])
            rec = y_grid[gy, gx, ai]
            x1, y1, x2, y2 = _decode_one(
                rec, int(gx), int(gy),
                anchors[ai][0], anchors[ai][1],
                stride,
            )
            boxes.append(DecodedBox(
                cls=int(cls),
                label=labels[cls] if cls < len(labels) else f"c{cls}",
                score=score,
                obj=obj,
                cls_score=cls_score,
                x1=x1, y1=y1, x2=x2, y2=y2,
                scale=scale_i,
                gy=int(gy),
                gx=int(gx),
                anchor=int(ai),
            ))

    return boxes


def iou_xyxy(a, b) -> float:
    x1 = max(a[0], b[0])
    y1 = max(a[1], b[1])
    x2 = min(a[2], b[2])
    y2 = min(a[3], b[3])
    inter = max(0.0, x2 - x1) * max(0.0, y2 - y1)
    area_a = max(0.0, a[2] - a[0]) * max(0.0, a[3] - a[1])
    area_b = max(0.0, b[2] - b[0]) * max(0.0, b[3] - b[1])
    return inter / (area_a + area_b - inter + 1e-9)


def nms_class_wise(boxes, iou_threshold: float = 0.5):
    """Class-wise NMS (matches nms_option=false in AI Cube)."""
    kept = []
    for cls in {b.cls for b in boxes}:
        cls_boxes = sorted([b for b in boxes if b.cls == cls],
                           key=lambda b: -b.score)
        while cls_boxes:
            head = cls_boxes.pop(0)
            kept.append(head)
            cls_boxes = [b for b in cls_boxes
                         if iou_xyxy(head.box, b.box) < iou_threshold]
    return sorted(kept, key=lambda b: -b.score)

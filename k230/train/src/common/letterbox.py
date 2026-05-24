"""Pure-numpy letterbox matching the K230D ai2d preprocessing.

The on-device pipeline uses:
    ai2d.set_pad_param(True, [0,0,0,0,top,bottom,left,right], 0, [114,114,114])
    ai2d.set_resize_param(True, nn.interp_method.tf_bilinear, nn.interp_mode.half_pixel)

ratio = min(model_w / ori_w, model_h / ori_h)
new_w = round(ratio * ori_w)
new_h = round(ratio * ori_h)
top   = (model_h - new_h) // 2
bottom= model_h - new_h - top
left  = (model_w - new_w) // 2
right = model_w - new_w - left
pad value = 114 (gray) for all 3 channels.

This module reproduces that on the PC so PC evaluation and K230D inference
see equivalent inputs.
"""
from __future__ import annotations

import numpy as np

try:
    import cv2
except ImportError as e:  # pragma: no cover
    raise ImportError("opencv-python is required for letterbox preprocessing") from e


PAD_VALUE = 114


def letterbox_bgr(img_bgr: np.ndarray, model_w: int, model_h: int,
                  pad_value: int = PAD_VALUE):
    """Letterbox a BGR image to (model_w, model_h).

    Returns (letterboxed_bgr, ratio, (left_pad, top_pad)).
    The transform is: letter_x = ori_x * ratio + left_pad.
    """
    ori_h, ori_w = img_bgr.shape[:2]
    ratio = min(model_w / ori_w, model_h / ori_h)
    new_w = int(round(ori_w * ratio))
    new_h = int(round(ori_h * ratio))
    resized = cv2.resize(img_bgr, (new_w, new_h), interpolation=cv2.INTER_LINEAR)
    top = (model_h - new_h) // 2
    bottom = model_h - new_h - top
    left = (model_w - new_w) // 2
    right = model_w - new_w - left
    letter = cv2.copyMakeBorder(
        resized, top, bottom, left, right,
        cv2.BORDER_CONSTANT,
        value=(pad_value, pad_value, pad_value),
    )
    return letter, ratio, (left, top)


def to_input_tensor(letter_bgr: np.ndarray, channel_order: str = "rgb") -> np.ndarray:
    """Convert a letterboxed BGR image to NCHW uint8 batch.

    channel_order: 'rgb' (default), 'bgr', or 'gray3'.
    """
    if channel_order == "rgb":
        img = cv2.cvtColor(letter_bgr, cv2.COLOR_BGR2RGB)
    elif channel_order == "bgr":
        img = letter_bgr
    elif channel_order == "gray3":
        gray = cv2.cvtColor(letter_bgr, cv2.COLOR_BGR2GRAY)
        img = np.stack([gray, gray, gray], axis=2)
    else:
        raise ValueError(f"Unknown channel_order: {channel_order}")
    return img.transpose(2, 0, 1)[None].astype(np.uint8)


def transform_box_to_letter(gt_xyxy_orig, ratio: float, pad_lt) -> list:
    """Map a GT box in original coords to letterboxed (model) coords."""
    left, top = pad_lt
    x1, y1, x2, y2 = gt_xyxy_orig
    return [
        x1 * ratio + left,
        y1 * ratio + top,
        x2 * ratio + left,
        y2 * ratio + top,
    ]


def inverse_transform_box(box_xyxy_letter, ratio: float, pad_lt,
                          ori_w: int, ori_h: int) -> list:
    """Map a decoded box in letterboxed coords back to original coords (clamped)."""
    left, top = pad_lt
    x1 = (box_xyxy_letter[0] - left) / ratio
    y1 = (box_xyxy_letter[1] - top) / ratio
    x2 = (box_xyxy_letter[2] - left) / ratio
    y2 = (box_xyxy_letter[3] - top) / ratio
    return [
        max(0.0, min(ori_w, x1)),
        max(0.0, min(ori_h, y1)),
        max(0.0, min(ori_w, x2)),
        max(0.0, min(ori_h, y2)),
    ]

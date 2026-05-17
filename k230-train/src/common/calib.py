"""Calibration tensor loader.

Loads N images from a directory, letterboxes to model resolution, returns
an NCHW float32 batch in the requested input range.

The float32 range MUST match the input_range/mean/std that nncase
CompileOptions are configured for. Otherwise the calibration data is in a
different distribution from what the .kmodel expects at inference and
quantization scales will be wrong.
"""
from __future__ import annotations

import glob
import os
import random
from typing import List

import numpy as np

try:
    import cv2
except ImportError as e:  # pragma: no cover
    raise ImportError("opencv-python is required for calibration loading") from e

from .letterbox import letterbox_bgr, PAD_VALUE


def list_images(directory: str) -> List[str]:
    exts = ("*.jpg", "*.jpeg", "*.png", "*.bmp")
    files = []
    for ext in exts:
        files.extend(glob.glob(os.path.join(directory, ext)))
        files.extend(glob.glob(os.path.join(directory, ext.upper())))
    return sorted(files)


def load_calibration_tensor(directory: str, count: int,
                            width: int, height: int,
                            preprocess_mode: str,
                            channel_order: str = "rgb",
                            seed: int = 42) -> np.ndarray:
    """Load `count` images and return an NCHW float32 calibration batch.

    preprocess_mode:
      "baked_imagenet" -> range [0,1], values are float (we DO the /255 here,
        so nncase calibration sees the same distribution that runtime will
        after the input_range=[0,1] dequant).
      "raw_01"         -> range [0,1], no mean/std subtraction.
      "raw_0_255"      -> range [0,255], no scaling.

    For preprocess=True with input_type=uint8, nncase wants calibration data
    in the same numeric range that the dequantized input will take. So:

      baked_imagenet / raw_01: calib values in [0,1]
      raw_0_255:               calib values in [0,255]
    """
    files = list_images(directory)
    if not files:
        raise FileNotFoundError(f"No images in {directory}")

    rng = random.Random(seed)
    if len(files) > count:
        files = rng.sample(files, count)
    else:
        # Cycle if directory is smaller than requested count.
        while len(files) < count:
            files = files + [rng.choice(files)]
        files = files[:count]

    batch = np.zeros((count, 3, height, width), dtype=np.float32)
    for i, fp in enumerate(files):
        bgr = cv2.imread(fp, cv2.IMREAD_COLOR)
        if bgr is None:
            raise IOError(f"Failed to read calibration image {fp}")
        letter, _, _ = letterbox_bgr(bgr, width, height, pad_value=PAD_VALUE)
        if channel_order == "rgb":
            img = cv2.cvtColor(letter, cv2.COLOR_BGR2RGB)
        elif channel_order == "bgr":
            img = letter
        elif channel_order == "gray3":
            gray = cv2.cvtColor(letter, cv2.COLOR_BGR2GRAY)
            img = np.stack([gray, gray, gray], axis=2)
        else:
            raise ValueError(f"Unknown channel_order: {channel_order}")
        chw = img.transpose(2, 0, 1).astype(np.float32)

        if preprocess_mode in ("baked_imagenet", "raw_01"):
            chw = chw / 255.0
        elif preprocess_mode == "raw_0_255":
            pass
        else:
            raise ValueError(f"Unknown preprocess_mode: {preprocess_mode}")

        batch[i] = chw

    return batch

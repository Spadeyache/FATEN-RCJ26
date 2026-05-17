"""nncase 2.9 PTQ option helpers + safe attribute setters.

Two purposes:
  - Centralise the PTQ option table so converters stay in sync.
  - Tolerate small API drift across nncase 2.9.x point releases (some builds
    expose dump_quant_error as a property, others as a method, etc.).
"""
from __future__ import annotations

import warnings


# (calibrate_method, activation quant type, weight quant type)
PTQ_OPTIONS = {
    0: ("NoClip", "uint8", "uint8"),
    1: ("NoClip", "uint8", "int16"),
    2: ("NoClip", "int16", "uint8"),
    3: ("Kld",    "uint8", "uint8"),
    4: ("Kld",    "uint8", "int16"),
    5: ("Kld",    "int16", "uint8"),
}


def ptq_describe(code: int) -> str:
    method, act, w = PTQ_OPTIONS[code]
    return f"{method} act={act} w={w}"


def safe_setattr(obj, name: str, value) -> bool:
    """Set obj.name = value if the attribute exists, else warn and skip.

    Returns True on success.
    """
    if hasattr(obj, name):
        try:
            setattr(obj, name, value)
            return True
        except Exception as e:
            warnings.warn(f"safe_setattr({name!r}): {e}")
            return False
    warnings.warn(
        f"nncase object {type(obj).__name__} has no attribute {name!r}; "
        "skipping (your nncase point release may differ)"
    )
    return False

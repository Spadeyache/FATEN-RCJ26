# app/camera.py -- owns the K230D's gc2093_csi2 sensor.
#
# Public API:
#   apply_config(sensor)   apply manual AEC/AGC/AWB/BLC + tone, runs
#                          /data/calibration/camera.json overrides
#   setup_button()         init BOOT button + capture dir
#   get_image(sensor)      sensor.snapshot() -- raw, exposure-locked
#   maybe_save(img)        save img to CAPTURE_DIR on BOOT-button press
#
# All tunables live in app/config.py. The calibration JSON at
# /data/calibration/camera.json can override any of:
#   exposure_us, gain_db, hmirror, vflip
# without editing config.py.

import os
import time
import ujson

from machine import Pin
from media.sensor import *

import config


# ---------------------------------------------------------------------------
# Internal state
# ---------------------------------------------------------------------------
_btn           = None
_last_press_ms = 0
_save_count    = 0


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def _try(obj, name, *a, **kw):
    """Call obj.name(*a, **kw) if it exists, else log and continue."""
    fn = getattr(obj, name, None)
    if fn is None:
        print("  camera: {} skipped (not on this build)".format(name))
        return
    try:
        fn(*a, **kw)
    except Exception as e:
        print("  camera: {} FAILED ({})".format(name, e))


def _mkdir_p(path):
    """Create path and all missing parents. CanMV's os.mkdir is single-level."""
    parts = path.strip("/").split("/")
    current = ""
    for p in parts:
        current += "/" + p
        try:
            os.mkdir(current)
        except OSError:
            pass


def _load_calibration():
    """Read /data/calibration/camera.json if present. Returns dict or {}."""
    try:
        with open(config.CAMERA_CALIB_PATH, "r") as f:
            cal = ujson.load(f)
        print("camera: loaded calibration from", config.CAMERA_CALIB_PATH)
        return cal
    except Exception as e:
        print("camera: no calibration ({}). Using config.py defaults.".format(e))
        return {}


def _camera_pixformat():
    pixformat = str(getattr(config, "CAMERA_PIXFORMAT", "GRAYSCALE")).upper()
    if pixformat == "RGB888":
        return Sensor.RGB888, "RGB888"
    return Sensor.GRAYSCALE, "GRAYSCALE"


def _readback(sensor):
    """Print actually-applied sensor state so we see what stuck."""
    print("--- sensor readback ---")
    for getter in ("get_exposure_us", "get_gain_db",
                   "get_rgb_gain_db", "get_blc_regs"):
        fn = getattr(sensor, getter, None)
        if fn is None:
            print("  {} : (not on this build)".format(getter))
            continue
        try:
            print("  {} = {}".format(getter, fn()))
        except Exception as e:
            print("  {} FAILED ({})".format(getter, e))
    print("-----------------------")


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------
def apply_config(sensor):
    """Apply every camera knob to `sensor`.

    Camera calibration JSON at /data/calibration/camera.json takes
    precedence over config.py. Fields supported in the JSON:

        {
          "exposure_us": 7894,
          "gain_db":      0.0,
          "hmirror":     false,
          "vflip":       false
        }

    Any subset is fine; missing fields fall back to config.py.
    """
    cal = _load_calibration()
    exposure_us = cal.get("exposure_us", config.EXPOSURE_US)
    gain_db     = cal.get("gain_db",     config.GAIN_DB)
    hmirror     = cal.get("hmirror",     config.HMIRROR)
    vflip       = cal.get("vflip",       config.VFLIP)

    sensor.set_framesize(width=config.WIDTH, height=config.HEIGHT, chn=CAM_CHN_ID_0)
    pixformat, pixformat_name = _camera_pixformat()
    sensor.set_pixformat(pixformat, chn=CAM_CHN_ID_0)

    _try(sensor, "set_hmirror", hmirror)
    _try(sensor, "set_vflip",   vflip)

    _try(sensor, "set_auto_exposure", config.AEC_AUTO, exposure_us=exposure_us)
    _try(sensor, "set_auto_gain",     config.AGC_AUTO, gain_db=gain_db)
    _try(sensor, "set_auto_whitebal", config.AWB_AUTO,
                                       rgb_gain_db=config.WHITEBAL_RGB_DB)
    _try(sensor, "set_auto_blc", config.BLC_AUTO)

    _readback(sensor)

    print("camera: requested expo={}us gain={}dB hmirror={} vflip={} pixformat={}".format(
          exposure_us, gain_db, hmirror, vflip, pixformat_name))
    return sensor


def setup_button():
    """Init the BOOT button + create CAPTURE_DIR."""
    global _btn, _save_count
    try:
        _btn = Pin(config.CAPTURE_BUTTON, Pin.IN, Pin.PULL_UP)
    except Exception as e:
        print("camera: button init FAILED ({}); capture disabled".format(e))
        _btn = None
    _mkdir_p(config.CAPTURE_DIR)
    try:
        _save_count = len(os.listdir(config.CAPTURE_DIR))
    except OSError:
        _save_count = 0
    print("camera: capture dir={} existing={} button={}".format(
        config.CAPTURE_DIR, _save_count, "ready" if _btn else "disabled"))


def get_image(sensor):
    """Grab one frame. Raw exposure-locked sensor output -- no software stretching."""
    return sensor.snapshot()


def maybe_save(img):
    """If BOOT is pressed (debounced), save img to CAPTURE_DIR.

    Returns the filename or None.
    """
    global _last_press_ms, _save_count
    if _btn is None:
        return None
    now = time.ticks_ms()
    if _btn.value() == 0 and time.ticks_diff(now, _last_press_ms) > config.BUTTON_DEBOUNCE_MS:
        fn = "{}/cap_{:04d}.jpg".format(config.CAPTURE_DIR, _save_count)
        try:
            img.save(fn, quality=config.JPEG_QUALITY)
            _save_count += 1
            _last_press_ms = now
            return fn
        except Exception as e:
            print("camera: save FAILED ({})".format(e))
    return None

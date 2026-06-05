# K230D Zero / CanMV v1.5-legacy
#
# camera_vision.py -- owns ALL camera operations.
#
# Public surface (called from main.py / any deploy script):
#
#   apply_config(sensor)        -- one-time sensor config (manual AEC/AGC/AWB,
#                                  orientation, tone). Call AFTER sensor.reset()
#                                  and BEFORE sensor.run().
#
#   setup_button()              -- one-time BOOT-button + capture-dir init.
#
#   get_image(sensor)           -- snapshot + histogram equalization (the
#                                  K230D's `img.histeq()` -- plain global
#                                  histeq, not CLAHE; CanMV doesn't ship a
#                                  CLAHE primitive). Returns the processed
#                                  image, ready to display or feed to a model.
#
#   maybe_save(img)             -- save `img` if the BOOT button is pressed
#                                  (debounced). Returns the filename or None.
#
#   main()                      -- standalone preview / tuning loop. Run this
#                                  file directly to see what the current
#                                  tunables look like on-arena. Use it to
#                                  set EXPOSURE_US / GAIN_DB before training:
#                                  put the camera on the arena floor, watch
#                                  the rolling pixel min/max/mean printout,
#                                  edit the tunables here, re-run.
#
# All tunables live at the top of this file -- edit them here, nowhere else.

import os
import gc
import time

from machine import Pin
from media.sensor import *
from media.display import *
from media.media import *


# ============================================================================
# Tunables -- edit these.
# ============================================================================

# Frame -- explicit width/height/chn form is what CanMV v1.5-legacy on the
# gc2093_csi2 sensor module actually accepts. The older Sensor.VGA constant
# is unreliable on this binding.
WIDTH            = 640
HEIGHT           = 480
CAM_CHN          = CAM_CHN_ID_0
PIXFORMAT        = Sensor.GRAYSCALE        # gc2093 will give the Y channel

# Orientation
HMIRROR          = False
VFLIP            = False

# AEC -- auto exposure control. Manual.
AEC_AUTO         = False
EXPOSURE_US      = 8000                    # tune; typical 2000..30000

# AGC -- auto gain control. Manual.
AGC_AUTO         = False
GAIN_DB          = 0.0                     # tune; typical 0..24

# AWB -- auto white balance. Moot in GRAYSCALE but locking it is harmless.
AWB_AUTO         = False
WHITEBAL_RGB_DB  = (0.0, 0.0, 0.0)         # R, G, B gain in dB

# BLC -- black level correction. Locking it makes the dark floor deterministic.
BLC_AUTO         = False

# Equalization. DEFAULT: OFF.
# Histogram equalization (and CLAHE) couples the model input to scene
# content -- a frame with only debris and no silver victim gets its
# darkest debris pushed toward 255, so the model learns "brightest blob =
# silver" but at runtime debris-only scenes flip black -> silver. We
# normalize at the SENSOR (manual AEC/AGC/AWB/BLC above) instead of in
# software, and retrain on the same fixed exposure as deployment. Keep
# this off unless you have a specific reason to revert.
APPLY_HISTEQ     = False

# Capture
CAPTURE_DIR        = "/data/k230-train/captures"
CAPTURE_BUTTON     = 0                     # BOOT button GPIO on most K230D Zeros
BUTTON_DEBOUNCE_MS = 400
JPEG_QUALITY       = 95


# ============================================================================
# Internal state (module-level for MicroPython simplicity)
# ============================================================================
_btn           = None
_last_press_ms = 0
_save_count    = 0


# ============================================================================
# Helpers
# ============================================================================

def _try(obj, name, *a, **kw):
    """Call obj.<name>(*a, **kw) if it exists; otherwise log and continue.

    Looking the method up by string name (instead of letting the caller
    write `obj.method`) means a missing method is caught here -- doing it
    the other way raises AttributeError BEFORE _try() runs.
    """
    fn = getattr(obj, name, None)
    if fn is None:
        print("  set {} SKIPPED (no such method on this CanMV build)".format(name))
        return
    try:
        fn(*a, **kw)
    except Exception as e:
        print("  set {} FAILED ({})".format(name, e))


def _mkdir_p(path):
    """Create `path` and all missing parents. CanMV os.mkdir is single-level."""
    parts = path.strip("/").split("/")
    current = ""
    for p in parts:
        current += "/" + p
        try:
            os.mkdir(current)
        except OSError:
            pass


# ============================================================================
# Public API
# ============================================================================

def apply_config(sensor):
    """Apply every camera knob to `sensor`. Returns `sensor`.

    CanMV v1.5-legacy + gc2093_csi2 specifics (confirmed by REPL dir()):
      * `set_framesize` / `set_pixformat` need `width=, height=, chn=` /
        `chn=` kwargs.
      * There is NO standalone `set_exposure` / `set_gain`. Manual exposure
        and gain go through `set_auto_exposure(False, exposure_us=N)` and
        `set_auto_gain(False, gain_db=N)`.
      * `set_auto_blc(False)` locks black level correction.
      * Driver does NOT have `set_contrast` / `set_brightness` /
        `set_saturation` -- removed from this config.
    """
    sensor.set_framesize(width=WIDTH, height=HEIGHT, chn=CAM_CHN)
    sensor.set_pixformat(PIXFORMAT, chn=CAM_CHN)

    _try(sensor, "set_hmirror", HMIRROR)
    _try(sensor, "set_vflip",   VFLIP)

    _try(sensor, "set_auto_exposure", AEC_AUTO, exposure_us=EXPOSURE_US)
    _try(sensor, "set_auto_gain",     AGC_AUTO, gain_db=GAIN_DB)
    _try(sensor, "set_auto_whitebal", AWB_AUTO, rgb_gain_db=WHITEBAL_RGB_DB)
    _try(sensor, "set_auto_blc",      BLC_AUTO)

    _readback(sensor)

    print("camera_vision: requested expo={}us gain={}dB mirror={} vflip={} "
          "blc_auto={} histeq={}".format(
              EXPOSURE_US, GAIN_DB, HMIRROR, VFLIP, BLC_AUTO, APPLY_HISTEQ))
    return sensor


def _readback(sensor):
    """Print the actually-applied sensor state so you can see what stuck.

    If you ask for EXPOSURE_US=8000 but the driver clamps to a nearer
    frame-row count, the readback shows the clamped value. Same for gain.
    """
    print("--- sensor readback ---")
    for getter in ("get_exposure_us", "get_gain_db",
                   "get_rgb_gain_db", "get_blc_regs"):
        fn = getattr(sensor, getter, None)
        if fn is None:
            print("  {} : (not on this build)".format(getter))
            continue
        try:
            v = fn()
            print("  {} = {}".format(getter, v))
        except Exception as e:
            print("  {} FAILED ({})".format(getter, e))
    print("-----------------------")


def setup_button():
    """Init the BOOT button and the capture directory."""
    global _btn, _save_count
    try:
        _btn = Pin(CAPTURE_BUTTON, Pin.IN, Pin.PULL_UP)
    except Exception as e:
        print("camera_vision: button init FAILED ({}); capture disabled".format(e))
        _btn = None
    _mkdir_p(CAPTURE_DIR)
    try:
        _save_count = len(os.listdir(CAPTURE_DIR))
    except OSError:
        _save_count = 0
    print("camera_vision: capture dir={} existing={} button={}".format(
        CAPTURE_DIR, _save_count, "ready" if _btn else "disabled"))


def get_image(sensor):
    """Grab one frame and apply equalization. Returns the processed image."""
    img = sensor.snapshot()
    if APPLY_HISTEQ:
        img.histeq()
    return img


def maybe_save(img):
    """If the BOOT button is pressed (with debounce), save img to disk.

    Returns the saved filename or None. The save happens on the image
    AS-IS, so what hits the SD card is what your model would see.
    """
    global _last_press_ms, _save_count
    if _btn is None:
        return None
    now = time.ticks_ms()
    if _btn.value() == 0 and time.ticks_diff(now, _last_press_ms) > BUTTON_DEBOUNCE_MS:
        fn = "{}/cap_{:04d}.jpg".format(CAPTURE_DIR, _save_count)
        try:
            img.save(fn, quality=JPEG_QUALITY)
            _save_count += 1
            _last_press_ms = now
            return fn
        except Exception as e:
            print("camera_vision: save FAILED ({})".format(e))
    return None

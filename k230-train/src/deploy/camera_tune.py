# K230D Zero / CanMV v1.5-legacy
#
# Camera tuning + capture script. ALL auto controls (AEC, AGC, AWB) are
# disabled so the sensor delivers a STABLE, REPRODUCIBLE frame regardless
# of lighting changes. Tweak the constants at the top until the live
# preview looks right (good contrast on the line, no blown highlights on
# silver, no crushed blacks), then write those same values into the
# training/inference pipeline so the model sees the same image
# statistics in the field as in the dataset.
#
# How to use:
#   1. Copy this file to /data/k230-train/ on the SD card.
#   2. In the CanMV REPL:
#        import camera_tune
#        camera_tune.main()
#   3. Watch the IDE preview. Read the per-N-frame stats line:
#        f= 90 fps= 30.0 expo=4500us gain=2.3dB mean=128.3 p5=22 p95=210
#      mean/p5/p95 should sit roughly in 80..160 / 10..40 / 200..245 for
#      a well-exposed gray frame. If too dark -> raise EXPOSURE_US or
#      GAIN_DB. If blown out -> lower them.
#   4. Press the BOOT button (pin 0) to save the current frame to
#      /data/k230-train/captures/cap_NNNN.jpg for offline inspection.
#   5. Once happy, copy the constants below into your dataset capture
#      script AND into det_live_xy_yolov8.py so calibration / training /
#      inference all see the same look.

import os
import gc
import time

from machine import Pin
from media.sensor import *
from media.display import *
from media.media import *


# ============================================================================
# TUNABLES -- edit these. All other knobs below are derived defaults you
# can leave alone for the rescue-line use case.
# ============================================================================

# Camera basics
SENSOR_FRAMESIZE = Sensor.VGA              # 640x480
PIXFORMAT        = Sensor.GRAYSCALE        # match training pipeline

# Orientation -- flip these if the dataset was captured upside down or
# mirrored. Keep them consistent between dataset capture and inference.
HMIRROR          = False
VFLIP            = False

# AEC -- auto exposure control. False = manual; pick a fixed exposure in us.
AEC_AUTO         = False
EXPOSURE_US      = 8000                    # tune this; typical 2000..30000

# AGC -- auto gain control. False = manual; pick a fixed analog gain in dB.
AGC_AUTO         = False
GAIN_DB          = 0.0                     # tune this; typical 0..24 dB

# AWB -- auto white balance. We're in GRAYSCALE so this is moot, but turn
# it off so any RGB capture variant we add later starts predictable.
AWB_AUTO         = False
WHITEBAL_RGB_DB  = (0.0, 0.0, 0.0)         # R, G, B gain in dB

# Tone shaping -- usually leave 0; raise contrast if the line is hard to
# distinguish from the floor. Range -3..3 on most CanMV builds.
CONTRAST         = 0
BRIGHTNESS       = 0
SATURATION       = 0                       # ignored in GRAYSCALE

# Optional black-level / lens corrections (rarely needed). Comment out
# the calls below if your sensor doesn't accept them.
BLACK_LEVEL_CAL  = False                   # one-shot calibration on start

# Preview behaviour
APPLY_HISTEQ_PREVIEW = True                # match deploy/det_live_xy_yolov8
DRAW_HUD             = True                # overlay stats on the preview

# Capture
CAPTURE_DIR     = "/data/k230-train/captures"
CAPTURE_BUTTON  = 0                        # BOOT button on most K230D Zeros
BUTTON_DEBOUNCE_MS = 400
JPEG_QUALITY    = 95                       # 95 keeps detail for retraining

# Stats cadence
DEBUG_EVERY     = 30                       # frames between stats lines


# ============================================================================
# Helpers
# ============================================================================

def _try(label, fn, *a, **kw):
    """Run a sensor setter; swallow + log if the binding doesn't support it.

    Different K230D camera modules expose different subsets of these
    setters. We don't want a missing API to abort the whole tune script.
    """
    try:
        fn(*a, **kw)
        print("  set {} OK".format(label))
        return True
    except Exception as e:
        print("  set {} FAILED ({})".format(label, e))
        return False


def _mkdir_p(path):
    parts = path.strip("/").split("/")
    current = ""
    for p in parts:
        current += "/" + p
        try:
            os.mkdir(current)
        except OSError:
            pass


def _frame_stats(img):
    """Return mean / p5 / p95 brightness using ulab over the grayscale plane.

    Histogram of 64 bins is enough to drive tuning decisions and is much
    cheaper than full numpy percentile.
    """
    import ulab.numpy as np
    hwc = img.to_numpy_ref()
    if hwc.ndim == 3:
        plane = hwc[:, :, 0]
    else:
        plane = hwc
    flat = plane.reshape((plane.shape[0] * plane.shape[1],))
    # Cheap stats; ulab has min/max/mean but no percentile.
    mean = float(flat.sum()) / flat.size
    # Approximate p5 / p95 from a 64-bin histogram.
    hist = [0] * 64
    n = flat.size
    for i in range(n):
        b = int(flat[i]) >> 2
        if b < 0: b = 0
        elif b > 63: b = 63
        hist[b] += 1
    target_lo = int(n * 0.05)
    target_hi = int(n * 0.95)
    cum = 0; p5 = 0; p95 = 255
    for i, h in enumerate(hist):
        cum += h
        if cum <= target_lo:
            p5 = (i << 2) + 2
        if cum <= target_hi:
            p95 = (i << 2) + 2
    return mean, p5, p95


def _readback(sensor):
    """Try to read back the current exposure / gain values from the sensor."""
    expo = gain = None
    try: expo = sensor.get_exposure()
    except Exception: pass
    try: gain = sensor.get_gain()
    except Exception: pass
    return expo, gain


# ============================================================================
# Main
# ============================================================================

def main():
    print("=== camera_tune.py ===")
    _mkdir_p(CAPTURE_DIR)
    saved = len(os.listdir(CAPTURE_DIR))
    print("captures dir:", CAPTURE_DIR, " existing:", saved)

    btn = Pin(CAPTURE_BUTTON, Pin.IN, Pin.PULL_UP)
    last_press = 0

    sensor = Sensor()
    sensor.reset()
    sensor.set_framesize(SENSOR_FRAMESIZE)
    sensor.set_pixformat(PIXFORMAT)

    # Orientation
    _try("hmirror",   sensor.set_hmirror,   HMIRROR)
    _try("vflip",     sensor.set_vflip,     VFLIP)

    # AEC -- exposure
    _try("auto_exposure off",
         sensor.set_auto_exposure, AEC_AUTO, exposure_us=EXPOSURE_US)
    # Some bindings need a follow-up explicit set_exposure() to apply.
    _try("exposure",  sensor.set_exposure,  EXPOSURE_US)

    # AGC -- analog gain
    _try("auto_gain off",
         sensor.set_auto_gain, AGC_AUTO, gain_db=GAIN_DB)
    _try("gain",      sensor.set_gain,      GAIN_DB)

    # AWB -- white balance (no-op in grayscale, kept for forward compat)
    _try("auto_whitebal off",
         sensor.set_auto_whitebal, AWB_AUTO, rgb_gain_db=WHITEBAL_RGB_DB)

    # Tone
    _try("contrast",   sensor.set_contrast,   CONTRAST)
    _try("brightness", sensor.set_brightness, BRIGHTNESS)
    _try("saturation", sensor.set_saturation, SATURATION)

    # Optional black-level cal
    if BLACK_LEVEL_CAL:
        _try("black_level_cal", sensor.set_blacklevel, True)

    Display.init(Display.VIRT, sensor.width(), sensor.height(), to_ide=True)
    MediaManager.init()
    sensor.run()
    time.sleep_ms(200)

    print("\nRunning. BOOT button to save. Ctrl+C to stop.\n")
    expo, gain = _readback(sensor)
    print("readback:  exposure=", expo, " gain=", gain)

    frame = 0
    t_window = time.ticks_ms()
    last_stats_mean = 0.0
    last_stats_p5 = 0
    last_stats_p95 = 255

    try:
        while True:
            img = sensor.snapshot()
            if APPLY_HISTEQ_PREVIEW:
                img.histeq()

            # BOOT button capture (PRE-overlay so saved JPG is clean).
            now = time.ticks_ms()
            if btn.value() == 0 and time.ticks_diff(now, last_press) > BUTTON_DEBOUNCE_MS:
                fn = "{}/cap_{:04d}.jpg".format(CAPTURE_DIR, saved)
                img.save(fn, quality=JPEG_QUALITY)
                saved += 1
                last_press = now
                print("saved ->", fn)

            # Stats + HUD
            if frame % DEBUG_EVERY == 0:
                last_stats_mean, last_stats_p5, last_stats_p95 = _frame_stats(img)
                now2 = time.ticks_ms()
                fps = DEBUG_EVERY * 1000 / max(1, time.ticks_diff(now2, t_window))
                expo, gain = _readback(sensor)
                print("f={:5d} fps={:5.2f} expo={} gain={} "
                      "mean={:6.1f} p5={:3d} p95={:3d}".format(
                          frame, fps, expo, gain,
                          last_stats_mean, last_stats_p5, last_stats_p95))
                t_window = now2

            if DRAW_HUD:
                img.draw_string_advanced(
                    4, 4, 14,
                    "expo={} gain={} mean={:.0f}".format(
                        expo, gain, last_stats_mean),
                    color=(255, 255, 255))
                img.draw_string_advanced(
                    4, 22, 14,
                    "p5={} p95={} hist={}".format(
                        last_stats_p5, last_stats_p95,
                        "ON" if APPLY_HISTEQ_PREVIEW else "OFF"),
                    color=(255, 255, 255))

            Display.show_image(img)
            gc.collect()
            frame += 1

    except KeyboardInterrupt:
        print("\nstopping...")
    finally:
        sensor.stop()
        Display.deinit()
        MediaManager.deinit()
        print("done.")


if __name__ == "__main__":
    main()

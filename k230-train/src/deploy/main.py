# K230D Zero / CanMV v1.5-legacy
#
# main.py -- runs at boot on CanMV.
#
# 1. Applies the manual camera config (all auto controls OFF).
# 2. Shows the live grayscale frame on the IDE preview, no inference.
#
# Drop in /data/k230-train/main.py on the SD card. Edit the constants in
# the CAMERA CONFIG block; everything else is plumbing.

import gc
import time

from media.sensor import *
from media.display import *
from media.media import *


# ============================================================================
# CAMERA CONFIG -- edit these. Same values you found with camera_tune.py.
# Once you're happy, copy this whole block into det_live_YOLOv8_anchor-free.py
# so dataset capture / training calibration / on-robot inference all match.
# ============================================================================

# Frame
SENSOR_FRAMESIZE = Sensor.VGA              # 640x480
PIXFORMAT        = Sensor.GRAYSCALE        # match training pipeline

# Orientation
HMIRROR          = False
VFLIP            = False

# AEC -- auto exposure control. Manual.
AEC_AUTO         = False
EXPOSURE_US      = 8000                    # tune

# AGC -- auto gain control. Manual.
AGC_AUTO         = False
GAIN_DB          = 0.0                     # tune

# AWB -- auto white balance. Manual (moot in GRAYSCALE).
AWB_AUTO         = False
WHITEBAL_RGB_DB  = (0.0, 0.0, 0.0)

# Tone (usually 0)
CONTRAST         = 0
BRIGHTNESS       = 0
SATURATION       = 0

# Preview tweaks
APPLY_HISTEQ     = True                    # matches dataset capture path
SETTLE_MS        = 200


# ============================================================================
# Plumbing
# ============================================================================

def _try(label, fn, *a, **kw):
    """Run a sensor setter and don't crash if the binding doesn't accept it."""
    try:
        fn(*a, **kw)
    except Exception as e:
        print("  set {} FAILED ({})".format(label, e))


def apply_camera_config(sensor):
    """Set every camera knob, then return."""
    sensor.set_framesize(SENSOR_FRAMESIZE)
    sensor.set_pixformat(PIXFORMAT)

    _try("hmirror",   sensor.set_hmirror,   HMIRROR)
    _try("vflip",     sensor.set_vflip,     VFLIP)

    _try("auto_exposure",
         sensor.set_auto_exposure, AEC_AUTO, exposure_us=EXPOSURE_US)
    _try("exposure",  sensor.set_exposure,  EXPOSURE_US)

    _try("auto_gain",
         sensor.set_auto_gain, AGC_AUTO, gain_db=GAIN_DB)
    _try("gain",      sensor.set_gain,      GAIN_DB)

    _try("auto_whitebal",
         sensor.set_auto_whitebal, AWB_AUTO, rgb_gain_db=WHITEBAL_RGB_DB)

    _try("contrast",   sensor.set_contrast,   CONTRAST)
    _try("brightness", sensor.set_brightness, BRIGHTNESS)
    _try("saturation", sensor.set_saturation, SATURATION)

    print("Camera configured: expo={}us gain={}dB mirror={} vflip={} histeq={}".format(
        EXPOSURE_US, GAIN_DB, HMIRROR, VFLIP, APPLY_HISTEQ))


def main():
    print("=== main.py (config + preview only) ===")

    sensor = Sensor()
    sensor.reset()
    apply_camera_config(sensor)

    Display.init(Display.VIRT, sensor.width(), sensor.height(), to_ide=True)
    MediaManager.init()
    sensor.run()
    time.sleep_ms(SETTLE_MS)

    print("Preview running. Ctrl+C to stop.")
    try:
        while True:
            img = sensor.snapshot()
            if APPLY_HISTEQ:
                img.histeq()
            Display.show_image(img)
            gc.collect()
    except KeyboardInterrupt:
        print("\nstopping...")
    finally:
        sensor.stop()
        Display.deinit()
        MediaManager.deinit()
        print("done.")


if __name__ == "__main__":
    main()

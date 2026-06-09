# app/sensor_controls.py -- low-level camera tuning helpers.

import ujson

import camera_profile as profile
import paths


def try_call(obj, name, *args, **kwargs):
    """Call obj.name if the current firmware exposes it."""
    fn = getattr(obj, name, None)
    if fn is None:
        print("  sensor: {} skipped (not on this build)".format(name))
        return None
    try:
        return fn(*args, **kwargs)
    except Exception as e:
        print("  sensor: {} FAILED ({})".format(name, e))
        return None


def load_calibration():
    try:
        with open(paths.CAMERA_CALIB_PATH, "r") as f:
            cal = ujson.load(f)
        print("sensor: loaded calibration from", paths.CAMERA_CALIB_PATH)
        return cal
    except Exception as e:
        print("sensor: no calibration ({}). Using camera_profile.py defaults.".format(e))
        return {}


def readback(sensor):
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


def resolved_profile():
    cal = load_calibration()
    return {
        "exposure_us": cal.get("exposure_us", profile.EXPOSURE_US),
        "gain_db": cal.get("gain_db", profile.GAIN_DB),
        "hmirror": cal.get("hmirror", profile.HMIRROR),
        "vflip": cal.get("vflip", profile.VFLIP),
    }

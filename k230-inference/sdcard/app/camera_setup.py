# app/camera_setup.py -- owns the K230D gc2093_csi2 sensor setup.

import os
import time

from machine import Pin
from media.sensor import *

import camera_profile as profile
import paths
import sensor_controls


_btn = None
_last_press_ms = 0
_save_count = 0


def mkdir_p(path):
    parts = path.strip("/").split("/")
    current = ""
    for p in parts:
        current += "/" + p
        try:
            os.mkdir(current)
        except OSError:
            pass


def apply_config(sensor):
    cfg = sensor_controls.resolved_profile()

    sensor.set_framesize(width=profile.WIDTH, height=profile.HEIGHT, chn=CAM_CHN_ID_0)
    sensor.set_pixformat(Sensor.GRAYSCALE, chn=CAM_CHN_ID_0)

    sensor_controls.try_call(sensor, "set_hmirror", cfg["hmirror"])
    sensor_controls.try_call(sensor, "set_vflip", cfg["vflip"])
    sensor_controls.try_call(sensor, "set_auto_exposure",
                             profile.AEC_AUTO, exposure_us=cfg["exposure_us"])
    sensor_controls.try_call(sensor, "set_auto_gain",
                             profile.AGC_AUTO, gain_db=cfg["gain_db"])
    sensor_controls.try_call(sensor, "set_auto_whitebal",
                             profile.AWB_AUTO,
                             rgb_gain_db=profile.WHITEBAL_RGB_DB)
    sensor_controls.try_call(sensor, "set_auto_blc", profile.BLC_AUTO)

    sensor_controls.readback(sensor)
    print("camera: requested expo={}us gain={}dB hmirror={} vflip={}".format(
        cfg["exposure_us"], cfg["gain_db"], cfg["hmirror"], cfg["vflip"]))
    return sensor


def setup_button():
    global _btn, _save_count
    try:
        _btn = Pin(profile.CAPTURE_BUTTON, Pin.IN, Pin.PULL_UP)
    except Exception as e:
        print("camera: button init FAILED ({}); capture disabled".format(e))
        _btn = None
    mkdir_p(paths.CAPTURE_DIR)
    try:
        _save_count = len(os.listdir(paths.CAPTURE_DIR))
    except OSError:
        _save_count = 0
    print("camera: capture dir={} existing={} button={}".format(
        paths.CAPTURE_DIR, _save_count, "ready" if _btn else "disabled"))


def get_image(sensor):
    return sensor.snapshot()


def maybe_save(img):
    global _last_press_ms, _save_count
    if _btn is None:
        return None
    now = time.ticks_ms()
    if _btn.value() == 0 and time.ticks_diff(now, _last_press_ms) > profile.BUTTON_DEBOUNCE_MS:
        fn = "{}/cap_{:04d}.jpg".format(paths.CAPTURE_DIR, _save_count)
        try:
            img.save(fn, quality=profile.JPEG_QUALITY)
            _save_count += 1
            _last_press_ms = now
            return fn
        except Exception as e:
            print("camera: save FAILED ({})".format(e))
    return None

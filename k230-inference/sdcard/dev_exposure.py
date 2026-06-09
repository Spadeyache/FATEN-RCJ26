# dev_exposure.py -- print camera profile/readback and save BOOT-button frames.

import sys
import time

for _p in ("/sdcard/app", "/sdcard/config", "/sdcard/customLib"):
    if _p not in sys.path:
        sys.path.insert(0, _p)

from media.sensor import *
from media.display import *
from media.media import *

import camera_profile as profile
import camera_setup
import paths
import sensor_controls


def main():
    print("=== dev_exposure.py ===")
    print("calibration:", paths.CAMERA_CALIB_PATH)
    print("captures   :", paths.CAPTURE_DIR)
    print("resolved   :", sensor_controls.resolved_profile())

    sensor = Sensor()
    sensor.reset()
    camera_setup.apply_config(sensor)
    camera_setup.setup_button()

    if profile.SHOW_DISPLAY:
        Display.init(Display.VIRT, sensor.width(), sensor.height(), to_ide=True)
    MediaManager.init()
    sensor.run()
    time.sleep_ms(profile.SETTLE_MS)

    try:
        while True:
            img = camera_setup.get_image(sensor)
            saved = camera_setup.maybe_save(img)
            if saved:
                print("saved ->", saved)
            if profile.SHOW_DISPLAY:
                Display.show_image(img)
            time.sleep_ms(20)
    except KeyboardInterrupt:
        print("\nstopping...")
    finally:
        sensor.stop()
        if profile.SHOW_DISPLAY:
            Display.deinit()
        MediaManager.deinit()
        print("done.")


if __name__ == "__main__":
    main()

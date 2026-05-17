# Yolov8nlive_histeq.py
# Live YOLOv8 detection that matches the training preprocessing:
#   sensor RGB888 -> histeq -> YOLO inference
#
# Why: training images were saved as grayscale JPGs (which load as R=G=B
# 3-channel) with histeq applied. To match this distribution at inference,
# we need histeq on the live frame too. Without it, silver (the weaker
# class) underperforms more than black.
#
# Camera init mirrors cameraCapIMGv2.py exactly (proven stable). YOLO is
# bolted onto the same loop, no PipeLine. If this still disconnects, the
# problem is in the YOLOv8 wrapper's handling of a raw sensor frame and
# we'll need to fall back to PipeLine + software histeq.
#
# Press BOOT (pin 0) to save annotated frame. CTRL+C to stop.

from media.sensor import *
from media.display import *
from media.media import *
from libs.YOLO import YOLOv8
from machine import Pin
import os, sys, gc, time
import ulab.numpy as np
import image


# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------
kmodel_path = "/data/kmodel/best_640x480_k230.kmodel"
labels = ["black", "silver"]
model_input_size = [640, 480]
confidence_threshold = 0.01
nms_threshold        = 0.45
max_boxes_num        = 50

SAVE_DIR        = "/data/dataset/live_captures"
SAVE_ON_BUTTON  = True


def mkdir_p(path):
    parts = path.strip("/").split("/")
    current = ""
    for part in parts:
        current += "/" + part
        try:
            os.mkdir(current)
        except OSError:
            pass


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
if __name__ == "__main__":

    if SAVE_ON_BUTTON:
        mkdir_p(SAVE_DIR)
        saved = len(os.listdir(SAVE_DIR))
        btn = Pin(0, Pin.IN, Pin.PULL_UP)
        last_press = 0
        print("Press BOOT (pin 0) to save the current frame.")
        print("Existing saves in {}: {}".format(SAVE_DIR, saved))

    # ----- camera setup (mirrors cameraCapIMGv2.py exactly) -----
    sensor = Sensor()
    sensor.reset()
    sensor.set_framesize(Sensor.VGA)        # 640 x 480
    sensor.set_pixformat(Sensor.RGB888)     # 3-channel; for IR sensors R=G=B
    Display.init(Display.VIRT, sensor.width(), sensor.height(), to_ide=True)
    MediaManager.init()
    sensor.run()

    # ----- YOLO setup -----
    yolo = YOLOv8(
        task_type="detect",
        mode="video",
        kmodel_path=kmodel_path,
        labels=labels,
        rgb888p_size=[sensor.width(), sensor.height()],
        model_input_size=model_input_size,
        display_size=[sensor.width(), sensor.height()],
        conf_thresh=confidence_threshold,
        nms_thresh=nms_threshold,
        max_boxes_num=max_boxes_num,
        debug_mode=0,
    )
    yolo.config_preprocess()

    frame_count = 0
    t_window = time.ticks_ms()

    try:
        while True:
            # 1. grab frame
            img = sensor.snapshot()

            # 2. histogram equalize — match training preprocessing
            img.histeq()

            # 3. inference (img is RGB888, what YOLOv8 wrapper expects)
            res = yolo.run(img)

            # 4. draw boxes + show
            yolo.draw_result(res, img)
            Display.show_image(img)

            gc.collect()
            frame_count += 1

            # 5. button capture (saves the histeq'd, annotated frame)
            if SAVE_ON_BUTTON:
                now = time.ticks_ms()
                if btn.value() == 0 and time.ticks_diff(now, last_press) > 500:
                    fn = "{}/live_{:04d}.jpg".format(SAVE_DIR, saved)
                    img.save(fn, quality=90)
                    saved += 1
                    last_press = now
                    print("saved ->", fn)

            # 6. periodic stats
            if frame_count % 10 == 0:
                now = time.ticks_ms()
                win_ms = time.ticks_diff(now, t_window)
                fps = 10 * 1000 / win_ms if win_ms > 0 else 0
                n_det = len(res) if res else 0
                top_conf = 0.0
                if res:
                    try:
                        top_conf = max(d[4] for d in res)
                    except Exception:
                        pass
                print("frame {:>4}  fps={:.2f}  detections={}  top_conf={:.3f}".format(
                    frame_count, fps, n_det, top_conf))
                t_window = now

    except KeyboardInterrupt:
        print("\nstopping...")

    finally:
        yolo.deinit()
        sensor.stop()
        Display.deinit()
        MediaManager.deinit()
        print("done.")

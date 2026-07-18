# Yolov8nlive.py
# Live YOLOv8 detection on K230 using PipeLine (safer, proven stable).
#
# Note: this uses your existing INT8 kmodel by default — switching to the float
# kmodel earlier caused device hangs / USB disconnects. Get the live pipeline
# working with the quantized model first, then revisit float-vs-QAT separately.

from libs.PipeLine import PipeLine, ScopedTiming
from libs.YOLO import YOLOv8
from libs.Utils import *
from machine import Pin
import os, sys, gc, time
import ulab.numpy as np
import image


# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------
# Start with the quantized kmodel. Once live pipeline is confirmed working,
# you can swap in the float kmodel and see if it loads.
kmodel_path = "/data/kmodel/best_640x480_k230.kmodel"
# kmodel_path = "/data/kmodel/best_640x480_k230_float.kmodel"   # try once stable

labels = ["black", "silver"]
model_input_size = [640, 480]
rgb888p_size     = [640, 480]
display_mode     = "lcd"

# 0.01 = debug (see noise), raise once you confirm what real conf looks like.
confidence_threshold = 0.001
nms_threshold        = 0.45
max_boxes_num        = 50

# Save annotated frame on BOOT button press (pin 0)
SAVE_DIR        = "/data/dataset/live_captures"
SAVE_ON_BUTTON  = True

# Apply histogram equalization on each inference frame to match the training
# distribution (cameraCapIMGv2.py applies histeq before saving JPGs).
APPLY_HISTEQ    = True


def histeq_inplace(frame):
    """
    Approximate histogram equalization for IR frames via contrast stretching.

    Full per-image histeq isn't possible without ulab.numpy.histogram, which
    isn't available on this K230 firmware. Contrast stretching (linear remap
    of [min, max] -> [0, 255]) gives most of the visual benefit for IR input
    and runs in a few ms instead of seconds.

    Frame: (3, H, W) uint8 ulab.numpy array. R=G=B for IR cameras.
    """
    plane = frame[0]
    lo = int(np.min(plane))
    hi = int(np.max(plane))
    if hi <= lo:
        return frame
    scale = 255.0 / (hi - lo)
    for c in range(3):
        # Subtract minimum, scale to full 0-255 range, clip to uint8.
        # ulab promotes uint8 - int to float automatically; * scale stays float;
        # final cast back to uint8 via np.array(..., dtype=np.uint8).
        adj = (frame[c] - lo) * scale
        # Clamp via min/max in case scaling overshoots
        adj = np.maximum(adj, 0)
        adj = np.minimum(adj, 255)
        frame[c] = np.array(adj, dtype=np.uint8)
    return frame


def mkdir_p(path):
    parts = path.strip("/").split("/")
    current = ""
    for part in parts:
        current += "/" + part
        try:
            os.mkdir(current)
        except OSError:
            pass


if __name__ == "__main__":

    if SAVE_ON_BUTTON:
        mkdir_p(SAVE_DIR)
        saved = len(os.listdir(SAVE_DIR))
        btn = Pin(0, Pin.IN, Pin.PULL_UP)
        last_press = 0
        print("BOOT button (pin 0) saves the current frame.")
        print("Existing saves: {}".format(saved))

    # PipeLine handles camera + display + kpu input format. Single safe path.
    pl = PipeLine(rgb888p_size=rgb888p_size, display_mode=display_mode)
    pl.create()
    display_size = pl.get_display_size()

    yolo = YOLOv8(
        task_type="detect",
        mode="video",
        kmodel_path=kmodel_path,
        labels=labels,
        rgb888p_size=rgb888p_size,
        model_input_size=model_input_size,
        display_size=display_size,
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
            with ScopedTiming("total", 0):
                img = pl.get_frame()
                if APPLY_HISTEQ:
                    img = histeq_inplace(img)
                res = yolo.run(img)
                yolo.draw_result(res, pl.osd_img)
                pl.show_image()
                gc.collect()
                frame_count += 1

                if SAVE_ON_BUTTON:
                    now = time.ticks_ms()
                    if btn.value() == 0 and time.ticks_diff(now, last_press) > 500:
                        fn = "{}/live_{:04d}.jpg".format(SAVE_DIR, saved)
                        pl.osd_img.save(fn, quality=90)
                        saved += 1
                        last_press = now
                        print("saved ->", fn)

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
        pl.destroy()
        print("done.")

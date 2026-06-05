# Yolov8nrun_float.py
# Run the FLOAT kmodel on K230 (CPU execution, not KPU).
#
# Purpose: validate detection accuracy with full float precision before deciding
# whether to invest in QAT. Expected FPS: 1-4 (much slower than int8 KPU path).
#
# What changed vs. Yolov8nrun2.py:
#   - kmodel_path  -> points at the float kmodel
#   - conf_thresh  -> raised to 0.5 (float gives REAL confidences, not noise)
#   - nms_thresh   -> bumped to 0.45 (standard, was 0.15 to compensate for crushed conf)
#   - added per-frame FPS prints so you can decide if perf is acceptable

from libs.PipeLine import PipeLine
from libs.YOLO import YOLOv8
from libs.Utils import *
import os, sys, gc, time
import ulab.numpy as np
import image


if __name__ == "__main__":

    # ---------------------------------------------------------------------
    # Float kmodel — runs on RISC-V CPU, not KPU. Slower but accurate.
    # Copy the float kmodel from your convert container to this path.
    # ---------------------------------------------------------------------
    kmodel_path = "/data/kmodel/best_640x480_float.kmodel"
    labels = ["black", "silver"]
    model_input_size = [640, 480]

    display_mode = "lcd"
    rgb888p_size = [640, 480]

    # Float model gives genuine sigmoid outputs in [0,1].
    # 0.5 = "model is at least moderately sure". Raise/lower based on what you see.
    confidence_threshold = 0.5
    nms_threshold = 0.45

    # Initialize pipeline
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
        max_boxes_num=50,
        debug_mode=0,
    )
    yolo.config_preprocess()

    # FPS tracker
    frame_count = 0
    fps_window_start = time.ticks_ms()

    try:
        while True:
            t0 = time.ticks_ms()

            img = pl.get_frame()
            res = yolo.run(img)
            yolo.draw_result(res, pl.osd_img)
            pl.show_image()
            gc.collect()

            # Per-frame timing
            t1 = time.ticks_ms()
            frame_ms = time.ticks_diff(t1, t0)
            frame_count += 1

            # Print rolling FPS every 10 frames
            if frame_count % 10 == 0:
                window_ms = time.ticks_diff(t1, fps_window_start)
                fps = 10 * 1000 / window_ms if window_ms > 0 else 0
                n_det = len(res) if res else 0
                print("frame {:>4}  this={:>4}ms  avg_fps={:.2f}  detections={}".format(
                    frame_count, frame_ms, fps, n_det))
                fps_window_start = t1
    finally:
        yolo.deinit()
        pl.destroy()

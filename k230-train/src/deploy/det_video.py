# K230D Zero / CanMV v1.5-legacy / nncase_runtime 2.9.0
#
# Live-camera AnchorBaseDet inference. Reads deploy_config.json so anchors /
# categories / img_size come from the variant being deployed.
#
# Based on the verified-working k230-inference/Yolov8nlive_aicube.py with
# minor cleanups: no histeq by default (kmodels from this branch are
# trained on RGB without histeq); explicit thresholds; per-frame FPS log.

import os
import gc
import time
import ujson

import nncase_runtime as nn
import ulab.numpy as np
import aicube
import image
from machine import Pin
from media.sensor import *
from media.display import *
from media.media import *


ROOT_PATH          = "/data/k230-train"
DEPLOY_CONFIG_PATH = ROOT_PATH + "/deploy_config.json"
SAVE_DIR           = ROOT_PATH + "/live_captures"

CONF_OVERRIDE = 0.2
NMS_OVERRIDE  = None
APPLY_HISTEQ  = False     # Only enable if your variant was trained on histeq'd images
SAVE_ON_BUTTON = True

STRIDES = [8, 16, 32]


def load_config():
    with open(DEPLOY_CONFIG_PATH, "r") as f:
        cfg = ujson.load(f)
    if not cfg["kmodel_path"].startswith("/"):
        cfg["kmodel_path"] = os.path.dirname(DEPLOY_CONFIG_PATH) + "/" + cfg["kmodel_path"]
    print("Loaded deploy_config.json:")
    for k in ("kmodel_path", "categories", "img_size", "num_classes",
              "confidence_threshold", "nms_threshold", "model_type"):
        print("  {:<22s} = {}".format(k, cfg.get(k)))
    if "_meta" in cfg:
        print("  _meta = ", cfg["_meta"])
    return cfg


def mkdir_p(path):
    parts = path.strip("/").split("/")
    current = ""
    for part in parts:
        current += "/" + part
        try:
            os.mkdir(current)
        except OSError:
            pass


def chw_from_image(img):
    """CanMV Image -> CHW uint8 with 3 channels."""
    hwc = img.to_numpy_ref()
    shape = hwc.shape
    if len(shape) == 2:
        H, W = shape
        plane = hwc
    elif len(shape) == 3 and shape[2] == 1:
        H, W, _ = shape
        plane = hwc[:, :, 0]
    else:
        H, W, C = shape
        return hwc.reshape((H * W, C)).transpose().copy().reshape((C, H, W))
    chw = np.zeros((3, H, W), dtype=np.uint8)
    chw[0] = plane
    chw[1] = plane
    chw[2] = plane
    return chw


def main():
    cfg          = load_config()
    kmodel_path  = cfg["kmodel_path"]
    labels       = cfg["categories"]
    img_size     = cfg["img_size"]
    num_classes  = cfg["num_classes"]
    conf_thr     = CONF_OVERRIDE if CONF_OVERRIDE is not None else cfg["confidence_threshold"]
    nms_thr      = NMS_OVERRIDE  if NMS_OVERRIDE  is not None else cfg["nms_threshold"]
    nms_option   = cfg["nms_option"]
    model_type   = cfg["model_type"]
    if model_type != "AnchorBaseDet":
        raise ValueError("This script supports AnchorBaseDet only, got: " + model_type)
    a = cfg["anchors"]
    anchors_flat = a[0] + a[1] + a[2]

    if SAVE_ON_BUTTON:
        mkdir_p(SAVE_DIR)
        saved = len(os.listdir(SAVE_DIR))
        btn = Pin(0, Pin.IN, Pin.PULL_UP)
        last_press = 0
        print("BOOT button (pin 0) saves the current annotated frame. saved={}".format(saved))

    sensor = Sensor()
    sensor.reset()
    sensor.set_framesize(Sensor.VGA)
    # Use color RGB by default; switch to GRAYSCALE if the trained model expects gray.
    sensor.set_pixformat(Sensor.RGB888)
    Display.init(Display.VIRT, sensor.width(), sensor.height(), to_ide=True)
    MediaManager.init()
    sensor.run()
    time.sleep_ms(200)

    sensor_w = sensor.width()
    sensor_h = sensor.height()
    model_w  = img_size[0]
    model_h  = img_size[1]

    kpu = nn.kpu()
    kpu.load_kmodel(kmodel_path)

    ai2d = nn.ai2d()
    ai2d.set_dtype(nn.ai2d_format.NCHW_FMT, nn.ai2d_format.NCHW_FMT,
                   np.uint8, np.uint8)
    ratio = min(model_w / sensor_w, model_h / sensor_h)
    new_w = int(ratio * sensor_w)
    new_h = int(ratio * sensor_h)
    dw    = (model_w - new_w) / 2
    dh    = (model_h - new_h) / 2
    top    = int(round(dh - 0.1))
    bottom = int(round(dh + 0.1))
    left   = int(round(dw - 0.1))
    right  = int(round(dw + 0.1))
    ai2d.set_pad_param(True, [0, 0, 0, 0, top, bottom, left, right], 0,
                       [114, 114, 114])
    ai2d.set_resize_param(True, nn.interp_method.tf_bilinear,
                          nn.interp_mode.half_pixel)
    ai2d_builder = ai2d.build([1, 3, sensor_h, sensor_w],
                              [1, 3, model_h, model_w])

    ai2d_out = nn.from_numpy(np.ones((1, 3, model_h, model_w), dtype=np.uint8))

    frame_count = 0
    t_window = time.ticks_ms()
    print("Running. Ctrl+C to stop.")
    try:
        while True:
            img = sensor.snapshot()
            if APPLY_HISTEQ:
                img.histeq()
            chw = chw_from_image(img)
            ai2d_input_tensor = nn.from_numpy(chw)
            ai2d_builder.run(ai2d_input_tensor, ai2d_out)
            del ai2d_input_tensor
            kpu.set_input_tensor(0, ai2d_out)
            kpu.run()

            results = []
            for i in range(kpu.outputs_size()):
                d = kpu.get_output_tensor(i)
                arr = d.to_numpy()
                total = 1
                for s in arr.shape:
                    total *= s
                results.append(arr.reshape((total,)))
                del d

            try:
                if len(results) == 3:
                    det = aicube.anchorbasedet_post_process(
                        results[0], results[1], results[2],
                        img_size, [sensor_w, sensor_h], STRIDES,
                        num_classes, conf_thr, nms_thr,
                        anchors_flat, nms_option,
                    )
                else:
                    det = aicube.anchorbasedet_post_process(
                        results[0], results[0], results[0],
                        img_size, [sensor_w, sensor_h], [STRIDES[0]] * 3,
                        num_classes, conf_thr, nms_thr,
                        anchors_flat, nms_option,
                    )
            except Exception as exc:
                det = []
                if frame_count < 3:
                    print("postprocess error:", exc)

            if det:
                for d in det:
                    cls_id, score, x1, y1, x2, y2 = d[0], d[1], d[2], d[3], d[4], d[5]
                    name = labels[cls_id] if cls_id < len(labels) else "c{}".format(cls_id)
                    img.draw_rectangle(int(x1), int(y1), int(x2 - x1),
                                       int(y2 - y1), color=(0, 255, 0))
                    img.draw_string_advanced(
                        int(x1), max(0, int(y1) - 20), 16,
                        "{} {:.2f}".format(name, score),
                        color=(255, 255, 255),
                    )
            Display.show_image(img)
            gc.collect()

            if SAVE_ON_BUTTON:
                now = time.ticks_ms()
                if btn.value() == 0 and time.ticks_diff(now, last_press) > 500:
                    fn = "{}/live_{:04d}.jpg".format(SAVE_DIR, saved)
                    img.save(fn, quality=90)
                    saved += 1
                    last_press = now
                    print("saved ->", fn)

            frame_count += 1
            if frame_count % 10 == 0:
                now = time.ticks_ms()
                fps = 10 * 1000 / max(1, time.ticks_diff(now, t_window))
                n_det = len(det) if det else 0
                top_conf = max([d[1] for d in det]) if det else 0.0
                print("frame {:>4}  fps={:.2f}  dets={}  top_conf={:.3f}".format(
                    frame_count, fps, n_det, top_conf))
                t_window = now

    except KeyboardInterrupt:
        print("\nstopping...")
    finally:
        if hasattr(kpu, "deinit"):
            kpu.deinit()
        sensor.stop()
        Display.deinit()
        MediaManager.deinit()
        nn.shrink_memory_pool()
        print("done.")


if __name__ == "__main__":
    main()

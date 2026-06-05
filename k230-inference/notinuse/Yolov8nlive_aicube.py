# Yolov8nlive_aicube.py
# Live anchor-based detection on K230 using an AI Cube-trained kmodel.
#
# Structure mirrors AI Cube's official sample (det_image.py) but for live video:
#   sensor RGB888 -> ai2d (resize+letterbox) -> kpu.run -> aicube.anchorbasedet_post_process
#
# No PipeLine, no libs.YOLO wrapper — those assume YOLOv8-format output.
# AI Cube's AnchorBaseDet uses a different output layout that only aicube's
# postprocess can decode.

import os, gc, time, ujson
import nncase_runtime as nn
import ulab.numpy as np
import aicube
import image
from machine import Pin
from media.sensor import *
from media.display import *
from media.media import *


# ---------------------------------------------------------------------------
# Config — overridden by deploy_config.json if present
# ---------------------------------------------------------------------------
DEPLOY_CONFIG_PATH = "/sdcard/mp_deployment_source/deploy_config.json"

# Hard-coded fallbacks if no deploy_config.json is found on the SD card
FALLBACK = {
    "kmodel_path":           "/data/kmodel/best_AnchorBaseDet_can3_5_n_20260514232500.kmodel",
    "categories":            ["black", "silver"],
    "img_size":              [640, 480],            # [W, H]
    "num_classes":           2,
    "confidence_threshold":  0.3,
    "nms_threshold":         0.45,
    "nms_option":            False,
    "model_type":            "AnchorBaseDet",
    # YOLOv5-style 3-stride anchors (placeholder — replace with the values
    # from your AI Cube deploy_config.json's "anchors" field).
    "anchors": [
        [10, 13, 16, 30, 33, 23],
        [30, 61, 62, 45, 59, 119],
        [116, 90, 156, 198, 373, 326],
    ],
}

STRIDES = [8, 16, 32]

SAVE_DIR        = "/data/dataset/live_captures"
SAVE_ON_BUTTON  = True


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def load_config():
    try:
        with open(DEPLOY_CONFIG_PATH, "r") as f:
            cfg = ujson.load(f)
        # Some AI Cube versions output kmodel name relative to the config dir;
        # patch it to an absolute path if it isn't already.
        if not cfg["kmodel_path"].startswith("/"):
            cfg["kmodel_path"] = os.path.dirname(DEPLOY_CONFIG_PATH) + "/" + cfg["kmodel_path"]
        print("Loaded deploy_config.json:")
        for k in ("kmodel_path", "categories", "img_size", "num_classes",
                 "confidence_threshold", "nms_threshold", "model_type"):
            print("  {:<22s} = {}".format(k, cfg.get(k)))
        return cfg
    except Exception as e:
        print("No deploy_config.json ({}). Using FALLBACK values.".format(e))
        return FALLBACK


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
    """
    Convert a CanMV Image to CHW uint8 with 3 channels for the kmodel.

    Grayscale snapshot (H, W) or (H, W, 1) -> broadcast to (3, H, W) so
    R=G=B=gray, matching how the training JPGs were decoded by AI Cube.
    """
    hwc = img.to_numpy_ref()
    shape = hwc.shape
    if len(shape) == 2:
        H, W = shape
        plane = hwc
    elif len(shape) == 3 and shape[2] == 1:
        H, W, _ = shape
        plane = hwc[:, :, 0]
    else:
        # Already 3-channel — use the standard HWC -> CHW path
        H, W, C = shape
        return hwc.reshape((H * W, C)).transpose().copy().reshape((C, H, W))
    chw = np.zeros((3, H, W), dtype=np.uint8)
    chw[0] = plane
    chw[1] = plane
    chw[2] = plane
    return chw


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    cfg          = load_config()
    kmodel_path  = cfg["kmodel_path"]
    labels       = cfg["categories"]
    img_size     = cfg["img_size"]              # [W, H] (model input)
    num_classes  = cfg["num_classes"]
    conf_thr     = cfg["confidence_threshold"]
    nms_thr      = cfg["nms_threshold"]
    nms_option   = cfg["nms_option"]
    model_type   = cfg["model_type"]
    anchors_lol  = cfg["anchors"]
    anchors_flat = anchors_lol[0] + anchors_lol[1] + anchors_lol[2]

    if SAVE_ON_BUTTON:
        mkdir_p(SAVE_DIR)
        saved = len(os.listdir(SAVE_DIR))
        btn = Pin(0, Pin.IN, Pin.PULL_UP)
        last_press = 0
        print("BOOT button (pin 0) saves the current annotated frame.")
        print("Existing saves: {}".format(saved))

    # ----- camera -----
    # Use GRAYSCALE to match the working cameraCapIMGv2.py setup and to match
    # the training data format (grayscale JPGs decoded as 3 equal channels).
    sensor = Sensor()
    sensor.reset()
    sensor.set_framesize(Sensor.VGA)
    sensor.set_pixformat(Sensor.GRAYSCALE)
    Display.init(Display.VIRT, sensor.width(), sensor.height(), to_ide=True)
    MediaManager.init()
    sensor.run()
    time.sleep_ms(200)   # settle

    sensor_w = sensor.width()
    sensor_h = sensor.height()
    model_w  = img_size[0]
    model_h  = img_size[1]

    # ----- KPU + ai2d (matches AI Cube sample, with letterbox padding) -----
    kpu = nn.kpu()
    kpu.load_kmodel(kmodel_path)

    ai2d = nn.ai2d()
    ai2d.set_dtype(nn.ai2d_format.NCHW_FMT, nn.ai2d_format.NCHW_FMT,
                   np.uint8, np.uint8)

    # Letterbox padding so aspect ratio is preserved
    ratio = min(model_w / sensor_w, model_h / sensor_h)
    new_w = int(ratio * sensor_w)
    new_h = int(ratio * sensor_h)
    dw    = (model_w - new_w) / 2
    dh    = (model_h - new_h) / 2
    top    = int(round(dh - 0.1))
    bottom = int(round(dh + 0.1))
    left   = int(round(dw - 0.1))
    right  = int(round(dw + 0.1))

    ai2d.set_pad_param(True, [0, 0, 0, 0, top, bottom, left, right], 0, [114, 114, 114])
    ai2d.set_resize_param(True, nn.interp_method.tf_bilinear, nn.interp_mode.half_pixel)
    ai2d_builder = ai2d.build([1, 3, sensor_h, sensor_w], [1, 3, model_h, model_w])

    # Pre-allocated output buffer for ai2d
    ai2d_out = nn.from_numpy(np.ones((1, 3, model_h, model_w), dtype=np.uint8))

    frame_count = 0
    t_window = time.ticks_ms()

    print("Running. Ctrl+C to stop.")
    try:
        while True:
            t_frame = time.ticks_ms()

            # 1. Capture frame (grayscale Image)
            img = sensor.snapshot()

            # 1b. Histogram equalize in-place to match training preprocessing
            #     (AI Cube was trained on histeq'd grayscale JPGs).
            img.histeq()

            # 2. Grayscale -> CHW 3-channel for the kmodel input
            chw = chw_from_image(img)
            ai2d_input_tensor = nn.from_numpy(chw)

            # 3. Preprocess (resize+letterbox to model input)
            ai2d_builder.run(ai2d_input_tensor, ai2d_out)
            del ai2d_input_tensor

            # 4. Inference
            kpu.set_input_tensor(0, ai2d_out)
            kpu.run()

            # 5. Gather outputs (1 or 3 depending on AI Cube head config)
            results = []
            for i in range(kpu.outputs_size()):
                data = kpu.get_output_tensor(i)
                arr = data.to_numpy()
                # aicube wants a flat 1D array per output
                total = 1
                for s in arr.shape:
                    total *= s
                results.append(arr.reshape((total,)))
                del data

            # 6. AnchorBase postprocess (Canaan's aicube module)
            #    Returns: list of [cls_id, score, x1, y1, x2, y2]
            try:
                if model_type == "AnchorBaseDet":
                    if len(results) == 3:
                        det = aicube.anchorbasedet_post_process(
                            results[0], results[1], results[2],
                            img_size, [sensor_w, sensor_h], STRIDES,
                            num_classes, conf_thr, nms_thr,
                            anchors_flat, nms_option,
                        )
                    elif len(results) == 1:
                        # Single-head AI Cube model (stride 8 only)
                        det = aicube.anchorbasedet_post_process(
                            results[0], results[0], results[0],
                            img_size, [sensor_w, sensor_h], [STRIDES[0]] * 3,
                            num_classes, conf_thr, nms_thr,
                            anchors_flat, nms_option,
                        )
                    else:
                        det = []
                else:
                    det = []
            except Exception as exc:
                det = []
                if frame_count < 3:
                    print("postprocess error:", exc)

            # 7. Draw + display
            if det:
                for d in det:
                    cls_id, score, x1, y1, x2, y2 = d[0], d[1], d[2], d[3], d[4], d[5]
                    name = labels[cls_id] if cls_id < len(labels) else "c{}".format(cls_id)
                    img.draw_rectangle(int(x1), int(y1), int(x2 - x1), int(y2 - y1), color=(0, 255, 0))
                    img.draw_string_advanced(int(x1), max(0, int(y1) - 20), 16,
                                             "{} {:.2f}".format(name, score),
                                             color=(255, 255, 255))
            Display.show_image(img)

            gc.collect()

            # 8. Button save
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
        kpu.deinit() if hasattr(kpu, "deinit") else None
        sensor.stop()
        Display.deinit()
        MediaManager.deinit()
        nn.shrink_memory_pool()
        print("done.")


if __name__ == "__main__":
    main()

# K230D Zero / CanMV v1.5-legacy
#
# Live grayscale + histogram-equalized AnchorBaseDet inference. Sends the
# SINGLE highest-confidence detection's center over UART to the Teensy.
#
# Why grayscale + histeq?  The training dataset (my_dataset/train/images)
# was captured with cameraCapIMGv2.py which feeds the sensor through
# Sensor.GRAYSCALE then img.histeq() before saving the JPG. The float
# kmodel learned those exact statistics, so we MUST do the same here.
#
# Wire: K230D UART1 (board-specific pins) <-> Teensy Serial5 (TX5=20, RX5=21)
# Baud: 115200.
#
# Teensy -> K230 (1 byte every 100 ms, existing protocol):
#   0x00 = idle, don't run inference
#   0x01 = run, send one best detection per frame
#
# K230 -> Teensy (8 bytes per frame, fixed size, big-endian coords):
#   [0xAA] [0x55] [type] [score] [x_hi] [x_lo] [y_hi] [y_lo]
#       type  = 0xFF (no detection), 0 (black), 1 (silver)
#       score = 0..255 (clip(conf * 255))
#       x, y  = pixel center of bbox in SENSOR coords (0..sensor_w / 0..sensor_h)
#                so Teensy can use them directly without knowing model size.
#
# This file deliberately drops the multi-detection [COUNT][TYPE,X,Y]xN packet
# the previous K230 firmware used. Always exactly one best detection,
# 8 bytes, no checksum (sync header 0xAA 0x55 is robust enough).

import os
import gc
import time
import ujson

import nncase_runtime as nn
import ulab.numpy as np
import aicube
import image
from machine import UART, FPIOA
from media.sensor import *
from media.display import *
from media.media import *


# ---------------------------------------------------------------------------
# Paths & tuning
# ---------------------------------------------------------------------------
ROOT_PATH          = "/data/k230-train"
DEPLOY_CONFIG_PATH = ROOT_PATH + "/deploy_config.json"

# Sensor / model
SENSOR_FRAMESIZE = Sensor.VGA          # 640x480 capture
APPLY_HISTEQ     = True                 # MUST match training preprocessing

# Inference gating
CONF_THRESHOLD   = 0.30                 # send only if best score >= this
NMS_THRESHOLD    = 0.50
STRIDES          = [8, 16, 32]

# UART
UART_DEVICE      = UART.UART1           # board-specific; change if your wiring is different
UART_BAUD        = 115200
UART_PARSE_BYTE  = True                 # parse Teensy -> K230 control byte (0x00/0x01)

# Output framing
SYNC0 = 0xAA
SYNC1 = 0x55
TYPE_NONE = 0xFF

# Display (optional; comment out if you don't want preview)
SHOW_DISPLAY = True

# Verbose debug prints
DEBUG_EVERY = 30                        # print stats every N frames


# ---------------------------------------------------------------------------
# UART helpers
# ---------------------------------------------------------------------------
def _open_uart():
    # CanMV v1.5-legacy: pinmux first if needed. The pin numbers below match
    # the K230D Zero default UART1 breakout; adjust if your board differs.
    try:
        fpioa = FPIOA()
        fpioa.set_function(11, FPIOA.UART1_TXD)   # K230 TXD pin
        fpioa.set_function(12, FPIOA.UART1_RXD)   # K230 RXD pin
    except Exception as e:
        print("FPIOA setup skipped:", e)
    u = UART(UART_DEVICE, baudrate=UART_BAUD, bits=8, parity=None, stop=1)
    return u


def _send_packet(u, type_id, score_u8, x_px, y_px):
    # Clamp to 16-bit unsigned for safety.
    x = max(0, min(65535, int(x_px)))
    y = max(0, min(65535, int(y_px)))
    s = max(0, min(255, int(score_u8)))
    t = type_id & 0xFF
    pkt = bytes([SYNC0, SYNC1, t, s, (x >> 8) & 0xFF, x & 0xFF,
                 (y >> 8) & 0xFF, y & 0xFF])
    u.write(pkt)


def _read_run_state(u, current):
    """Return True if Teensy commanded 'run' (0x01), False if 'idle' (0x00).

    Drains all pending bytes; takes the LAST one received as the state.
    If nothing arrived, returns `current` unchanged.
    """
    state = current
    n = u.any()
    if n:
        try:
            data = u.read(n) or b""
        except Exception:
            return current
        for b in data:
            if b == 0x01:
                state = True
            elif b == 0x00:
                state = False
            # other bytes ignored
    return state


# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------
def load_config():
    with open(DEPLOY_CONFIG_PATH, "r") as f:
        cfg = ujson.load(f)
    if not cfg["kmodel_path"].startswith("/"):
        cfg["kmodel_path"] = os.path.dirname(DEPLOY_CONFIG_PATH) + "/" + cfg["kmodel_path"]
    print("Loaded deploy_config:", DEPLOY_CONFIG_PATH)
    print("  kmodel: ", cfg["kmodel_path"])
    print("  img_size:", cfg["img_size"])
    print("  anchors:", cfg["anchors"])
    print("  categories:", cfg["categories"])
    return cfg


# ---------------------------------------------------------------------------
# Image -> CHW uint8 (3-channel from grayscale via broadcast)
# ---------------------------------------------------------------------------
def chw_from_grayscale(img):
    """img is a CanMV grayscale Image. Return CHW uint8 ulab.numpy of shape (3,H,W)."""
    hwc = img.to_numpy_ref()
    shape = hwc.shape
    if len(shape) == 2:
        H, W = shape
        plane = hwc
    elif len(shape) == 3 and shape[2] == 1:
        H, W, _ = shape
        plane = hwc[:, :, 0]
    else:
        # color path -- shouldn't normally hit, but handle it
        H, W, C = shape
        return hwc.reshape((H * W, C)).transpose().copy().reshape((C, H, W))
    chw = np.zeros((3, H, W), dtype=np.uint8)
    chw[0] = plane
    chw[1] = plane
    chw[2] = plane
    return chw


# ---------------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------------
def main():
    cfg = load_config()
    kmodel_path  = cfg["kmodel_path"]
    labels       = cfg["categories"]
    img_size     = cfg["img_size"]                  # [W, H]
    num_classes  = cfg["num_classes"]
    nms_option   = cfg["nms_option"]
    a            = cfg["anchors"]
    anchors_flat = a[0] + a[1] + a[2]

    # --- UART ---
    u = _open_uart()
    print("UART opened.")
    run_state = False

    # --- Camera (grayscale to match training) ---
    sensor = Sensor()
    sensor.reset()
    sensor.set_framesize(SENSOR_FRAMESIZE)
    sensor.set_pixformat(Sensor.GRAYSCALE)
    if SHOW_DISPLAY:
        Display.init(Display.VIRT, sensor.width(), sensor.height(), to_ide=True)
    MediaManager.init()
    sensor.run()
    time.sleep_ms(200)

    sensor_w = sensor.width()
    sensor_h = sensor.height()
    model_w  = img_size[0]
    model_h  = img_size[1]
    print(f"sensor {sensor_w}x{sensor_h} -> model {model_w}x{model_h}")

    # --- KPU ---
    kpu = nn.kpu()
    kpu.load_kmodel(kmodel_path)
    print("kmodel loaded.")

    # --- ai2d (letterbox 114-fill to model size) ---
    ai2d = nn.ai2d()
    ai2d.set_dtype(nn.ai2d_format.NCHW_FMT, nn.ai2d_format.NCHW_FMT,
                   np.uint8, np.uint8)
    ratio = min(model_w / sensor_w, model_h / sensor_h)
    new_w = int(ratio * sensor_w)
    new_h = int(ratio * sensor_h)
    dw = (model_w - new_w) / 2
    dh = (model_h - new_h) / 2
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

    frame = 0
    t_window = time.ticks_ms()
    print("Running. Send 0x01 over UART to start detection, 0x00 to pause.")
    try:
        while True:
            # 1. Drain UART for run/idle commands.
            run_state = _read_run_state(u, run_state)

            # 2. Capture (always; cheaper than restarting camera).
            img = sensor.snapshot()
            if APPLY_HISTEQ:
                img.histeq()                        # match training preprocessing

            # 3. If idle, push a "none" packet at low rate and skip inference.
            if not run_state:
                if frame % 10 == 0:
                    _send_packet(u, TYPE_NONE, 0, 0, 0)
                if SHOW_DISPLAY:
                    Display.show_image(img)
                frame += 1
                time.sleep_ms(20)
                continue

            # 4. Preprocess + KPU.
            chw = chw_from_grayscale(img)
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

            # 5. Postprocess via aicube. Returns list of [cls, score, x1, y1, x2, y2]
            #    in sensor-frame pixel coordinates (it handles the ai2d letterbox inverse).
            det = aicube.anchorbasedet_post_process(
                results[0], results[1], results[2],
                img_size, [sensor_w, sensor_h], STRIDES,
                num_classes, CONF_THRESHOLD, NMS_THRESHOLD,
                anchors_flat, nms_option,
            )

            # 6. Pick the single best detection.
            best = None
            if det:
                for d in det:
                    if best is None or d[1] > best[1]:
                        best = d

            # 7. Send packet.
            if best is not None:
                cls_id = int(best[0])
                score = float(best[1])
                x1, y1, x2, y2 = best[2], best[3], best[4], best[5]
                cx = int((x1 + x2) / 2)
                cy = int((y1 + y2) / 2)
                _send_packet(u, cls_id, int(score * 255), cx, cy)
                if SHOW_DISPLAY:
                    img.draw_rectangle(int(x1), int(y1), int(x2 - x1),
                                       int(y2 - y1), color=(255,))
                    img.draw_string_advanced(
                        int(x1), max(0, int(y1) - 20), 16,
                        "{} {:.2f}".format(labels[cls_id] if cls_id < len(labels)
                                            else "c", score),
                        color=(255,),
                    )
            else:
                _send_packet(u, TYPE_NONE, 0, 0, 0)

            # 8. Display preview.
            if SHOW_DISPLAY:
                Display.show_image(img)

            gc.collect()
            frame += 1
            if frame % DEBUG_EVERY == 0:
                now = time.ticks_ms()
                fps = DEBUG_EVERY * 1000 / max(1, time.ticks_diff(now, t_window))
                n_det = len(det) if det else 0
                top_s = best[1] if best else 0.0
                top_t = best[0] if best else -1
                print(f"f={frame:5d} fps={fps:5.2f} run={run_state} "
                      f"dets={n_det} top_cls={top_t} top_s={top_s:.3f}")
                t_window = now

    except KeyboardInterrupt:
        print("\nstopping...")
    finally:
        try: u.deinit()
        except Exception: pass
        if hasattr(kpu, "deinit"):
            kpu.deinit()
        sensor.stop()
        if SHOW_DISPLAY:
            Display.deinit()
        MediaManager.deinit()
        nn.shrink_memory_pool()
        print("done.")


if __name__ == "__main__":
    main()

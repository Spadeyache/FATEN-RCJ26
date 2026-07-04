# app/detector.py -- YOLOv8 anchor-free inference on the K230 KPU.
#
# Public API:
#   Detector(sensor) -> instance      load kmodel + build ai2d
#   .infer(img)      -> list of boxes [cls, score, x1, y1,x2, y2] in sensor coords
#   .release()                        deinit kpu / ai2d
#
# Reads /data/models/deploy_config.json and optional labels.txt to find the
# kmodel, preprocessing mode, thresholds, and class list.

import gc
import os
import time
import ujson

import nncase_runtime as nn
import ulab.numpy as np

import config
import yolov8_decode as yd


class Detector:
    def __init__(self, sensor, cfg_path=None):
        cfg = self._load_deploy_config(cfg_path)
        self._cfg          = cfg
        self.labels        = cfg["categories"]
        self.num_classes   = cfg["num_classes"]
        self.img_size      = cfg["img_size"]            # [W, H]
        self._kmodel_path  = cfg["kmodel_path"]
        self.conf_threshold = cfg.get("confidence_threshold", config.CONF_THRESHOLD)
        self.nms_threshold  = cfg.get("nms_threshold", config.NMS_THRESHOLD)
        self.preprocess     = cfg.get("_preprocess_mode", "letterbox")

        self.sensor_w = sensor.width()
        self.sensor_h = sensor.height()
        self.model_w  = self.img_size[0]
        self.model_h  = self.img_size[1]
        print("detector: sensor {}x{} -> model {}x{}".format(
            self.sensor_w, self.sensor_h, self.model_w, self.model_h))

        # KPU
        self._kpu = nn.kpu()
        self._kpu.load_kmodel(self._kmodel_path)

        # ai2d
        self._ai2d = nn.ai2d()
        self._ai2d.set_dtype(nn.ai2d_format.NCHW_FMT, nn.ai2d_format.NCHW_FMT,
                              np.uint8, np.uint8)
        if self.preprocess == "direct":
            self.ratio_x = self.model_w / self.sensor_w
            self.ratio_y = self.model_h / self.sensor_h
            self.ratio = self.ratio_x
            self.left = 0
            self.top = 0
        else:
            self.ratio = min(self.model_w / self.sensor_w,
                             self.model_h / self.sensor_h)
            self.ratio_x = self.ratio
            self.ratio_y = self.ratio
            new_w = int(self.ratio * self.sensor_w)
            new_h = int(self.ratio * self.sensor_h)
            dw = (self.model_w - new_w) / 2
            dh = (self.model_h - new_h) / 2
            self.top    = int(round(dh - 0.1))
            bottom      = int(round(dh + 0.1))
            self.left   = int(round(dw - 0.1))
            right       = int(round(dw + 0.1))
            self._ai2d.set_pad_param(True,
                                      [0, 0, 0, 0, self.top, bottom, self.left, right],
                                      0, [114, 114, 114])
        self._ai2d.set_resize_param(True, nn.interp_method.tf_bilinear,
                                     nn.interp_mode.half_pixel)
        self._ai2d_builder = self._ai2d.build(
            [1, 3, self.sensor_h, self.sensor_w],
            [1, 3, self.model_h, self.model_w])
        self._ai2d_out = nn.from_numpy(
            np.ones((1, 3, self.model_h, self.model_w), dtype=np.uint8))

    # ------------------------------------------------------------------
    def infer(self, img):
        """Run one inference. Returns list of [cls, score, x1, y1, x2, y2]
        in SENSOR pixel coords (post un-letterbox)."""
        t0 = time.ticks_ms() if config.PROFILE_TIMING else 0
        chw = self._chw_from_image(img)
        ai2d_input_tensor = nn.from_numpy(chw)
        self._ai2d_builder.run(ai2d_input_tensor, self._ai2d_out)
        del ai2d_input_tensor
        t1 = time.ticks_ms() if config.PROFILE_TIMING else 0

        self._kpu.set_input_tensor(0, self._ai2d_out)
        self._kpu.run()
        t2 = time.ticks_ms() if config.PROFILE_TIMING else 0

        results = []
        for i in range(self._kpu.outputs_size()):
            d = self._kpu.get_output_tensor(i)
            arr = d.to_numpy()
            total = 1
            for s in arr.shape:
                total *= s
            results.append(arr.reshape((total,)))
            del d

        if not results:
            return []

        boxes = yd.decode_anchorfree(
            results[0], self.num_classes, self.conf_threshold,
            num_anchors=config.NUM_ANCHORS_640x480)
        boxes = yd.nms_class_wise(boxes, self.nms_threshold)

        det = []
        for b in boxes:
            if self.preprocess == "direct":
                xyxy = self._unresize_box(b[2:6])
            else:
                xyxy = yd.unletterbox_box(b[2:6], self.ratio, self.left, self.top,
                                          self.sensor_w, self.sensor_h)
            det.append([b[0], b[1], xyxy[0], xyxy[1], xyxy[2], xyxy[3]])
        if config.PROFILE_TIMING:
            t3 = time.ticks_ms()
            print("timing: prep={}ms kpu={}ms post={}ms".format(
                time.ticks_diff(t1, t0), time.ticks_diff(t2, t1),
                time.ticks_diff(t3, t2)))
        return det

    # ------------------------------------------------------------------
    def release(self):
        # Stop the KPU, then drop ai2d + tensors so the nncase pool is actually
        # freed before the next model loads (otherwise B stacks on A -> OOM).
        kpu = getattr(self, "_kpu", None)
        if kpu is not None and hasattr(kpu, "deinit"):
            try: kpu.deinit()
            except Exception: pass
        self._kpu = None
        self._ai2d = None
        self._ai2d_builder = None
        self._ai2d_out = None
        try:
            gc.collect()
        except Exception:
            pass
        try:
            nn.shrink_memory_pool()
        except Exception:
            pass

    # ------------------------------------------------------------------
    @staticmethod
    def _load_deploy_config(path=None):
        if path is None:
            path = config.DEPLOY_CONFIG_PATH
        with open(path, "r") as f:
            cfg = ujson.load(f)
        # Resolve kmodel_path relative to the deploy_config's directory.
        if not cfg["kmodel_path"].startswith("/"):
            cfg["kmodel_path"] = path.rsplit("/", 1)[0] + "/" + cfg["kmodel_path"]
        cfg["kmodel_path"] = Detector._resolve_kmodel_path(cfg["kmodel_path"])
        # Only the default (victims) config uses the shared labels.txt; any other
        # config (e.g. points) carries its own categories in the JSON.
        if path == config.DEPLOY_CONFIG_PATH:
            cfg["categories"] = Detector._load_labels(cfg)
        else:
            cfg["categories"] = cfg.get("categories", [])
        cfg["num_classes"] = len(cfg["categories"])
        cfg["_preprocess_mode"] = Detector._preprocess_mode(cfg)
        print("detector: loaded deploy_config", path)
        print("  kmodel    :", cfg["kmodel_path"])
        print("  img_size  :", cfg["img_size"])
        print("  categories:", cfg["categories"])
        print("  model_type:", cfg["model_type"])
        print("  preprocess:", cfg["_preprocess_mode"])
        print("  conf/nms  :", cfg.get("confidence_threshold", config.CONF_THRESHOLD),
              "/", cfg.get("nms_threshold", config.NMS_THRESHOLD))
        if cfg.get("model_type") != "AnchorFreeDet":
            raise ValueError("detector requires model_type=AnchorFreeDet")
        return cfg

    @staticmethod
    def _load_labels(cfg):
        try:
            with open(config.LABELS_PATH, "r") as f:
                labels = []
                for line in f:
                    label = line.strip()
                    if label:
                        labels.append(label)
            if labels:
                return labels
        except Exception:
            pass
        return cfg.get("categories", [])

    @staticmethod
    def _preprocess_mode(cfg):
        preprocess = cfg.get("preprocess", {})
        resize = str(preprocess.get("resize", "")).lower()
        meta = cfg.get("_meta", {})
        if "direct" in resize or meta.get("use_letterbox") is False:
            return "direct"
        return "letterbox"

    @staticmethod
    def _resolve_kmodel_path(configured_path):
        candidates = [configured_path, config.KMODEL_DEFAULT,
                      config.MODELS_DIR + "/best.kmodel"]
        seen = []
        for p in candidates:
            if p in seen:
                continue
            seen.append(p)
            try:
                os.stat(p)
                if p != configured_path:
                    print("detector: using fallback kmodel path", p)
                return p
            except Exception:
                pass
        print("detector: kmodel not found. Checked:")
        for p in seen:
            print("  ", p)
        return configured_path

    def _unresize_box(self, box_xyxy):
        x1 = box_xyxy[0] / self.ratio_x
        y1 = box_xyxy[1] / self.ratio_y
        x2 = box_xyxy[2] / self.ratio_x
        y2 = box_xyxy[3] / self.ratio_y
        if x1 < 0: x1 = 0.0
        if y1 < 0: y1 = 0.0
        if x2 > self.sensor_w: x2 = float(self.sensor_w)
        if y2 > self.sensor_h: y2 = float(self.sensor_h)
        return [x1, y1, x2, y2]

    @staticmethod
    def _chw_from_image(img):
        """Convert a sensor snapshot to a (3, H, W) uint8 CHW plane.

        RGB888  -> to_numpy_ref() is (H, W, 3) RGB; split into planes directly
                   (matches the calibrated preprocessing: RGB, swapRB=false).
        GRAYSCALE -> single plane replicated across all 3 channels.
        """
        hwc = img.to_numpy_ref()
        shape = hwc.shape
        if len(shape) == 3 and shape[2] == 3:
            H, W, _ = shape
            chw = np.zeros((3, H, W), dtype=np.uint8)
            chw[0] = hwc[:, :, 0]   # R
            chw[1] = hwc[:, :, 1]   # G
            chw[2] = hwc[:, :, 2]   # B
            return chw
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

# app/victim_detector.py -- YOLOv8 anchor-free victim detection on K230 KPU.

import os
import ujson

import nncase_runtime as nn
import ulab.numpy as np

import camera_profile as profile
import paths
import yolov8_decode as yd


class VictimDetector:
    def __init__(self, sensor):
        cfg = self._load_deploy_config()
        self._cfg = cfg
        self.labels = cfg["categories"]
        self.num_classes = cfg["num_classes"]
        self.img_size = cfg["img_size"]
        self._kmodel_path = cfg["kmodel_path"]
        self.conf_threshold = cfg.get("confidence_threshold", profile.CONF_THRESHOLD)
        self.nms_threshold = cfg.get("nms_threshold", profile.NMS_THRESHOLD)
        self.preprocess = cfg.get("_preprocess_mode", "letterbox")

        self.sensor_w = sensor.width()
        self.sensor_h = sensor.height()
        self.model_w = self.img_size[0]
        self.model_h = self.img_size[1]
        print("detector: sensor {}x{} -> model {}x{}".format(
            self.sensor_w, self.sensor_h, self.model_w, self.model_h))

        self._kpu = nn.kpu()
        self._kpu.load_kmodel(self._kmodel_path)

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
            self.top = int(round(dh - 0.1))
            bottom = int(round(dh + 0.1))
            self.left = int(round(dw - 0.1))
            right = int(round(dw + 0.1))
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

    def infer(self, img):
        chw = self._chw_from_grayscale(img)
        ai2d_input_tensor = nn.from_numpy(chw)
        self._ai2d_builder.run(ai2d_input_tensor, self._ai2d_out)
        del ai2d_input_tensor

        self._kpu.set_input_tensor(0, self._ai2d_out)
        self._kpu.run()

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
            num_anchors=profile.NUM_ANCHORS_640x480)
        boxes = yd.nms_class_wise(boxes, self.nms_threshold)

        det = []
        for b in boxes:
            if self.preprocess == "direct":
                xyxy = self._unresize_box(b[2:6])
            else:
                xyxy = yd.unletterbox_box(b[2:6], self.ratio, self.left, self.top,
                                          self.sensor_w, self.sensor_h)
            det.append([b[0], b[1], xyxy[0], xyxy[1], xyxy[2], xyxy[3]])
        return det

    def release(self):
        if hasattr(self._kpu, "deinit"):
            try:
                self._kpu.deinit()
            except Exception:
                pass

    @staticmethod
    def _load_deploy_config():
        try:
            with open(paths.DEPLOY_CONFIG_PATH, "r") as f:
                cfg = ujson.load(f)
            if not cfg["kmodel_path"].startswith("/"):
                cfg["kmodel_path"] = paths.MODELS_DIR + "/" + cfg["kmodel_path"]
        except Exception as e:
            print("detector: no deploy_config ({}); using victim.kmodel defaults".format(e))
            cfg = {
                "kmodel_path": paths.KMODEL_PATH,
                "categories": profile.DEFAULT_CATEGORIES,
                "num_classes": len(profile.DEFAULT_CATEGORIES),
                "img_size": profile.DEFAULT_IMG_SIZE,
                "model_type": profile.DEFAULT_MODEL_TYPE,
            }
        cfg["kmodel_path"] = VictimDetector._resolve_kmodel_path(cfg["kmodel_path"])
        cfg["categories"] = VictimDetector._load_labels(cfg)
        cfg["num_classes"] = len(cfg["categories"])
        cfg["_preprocess_mode"] = VictimDetector._preprocess_mode(cfg)
        print("detector: kmodel    :", cfg["kmodel_path"])
        print("detector: img_size  :", cfg["img_size"])
        print("detector: categories:", cfg["categories"])
        print("detector: preprocess:", cfg["_preprocess_mode"])
        print("detector: conf/nms  :", cfg.get("confidence_threshold", profile.CONF_THRESHOLD),
              "/", cfg.get("nms_threshold", profile.NMS_THRESHOLD))
        if cfg.get("model_type") != "AnchorFreeDet":
            raise ValueError("detector requires model_type=AnchorFreeDet")
        return cfg

    @staticmethod
    def _load_labels(cfg):
        try:
            with open(paths.LABELS_PATH, "r") as f:
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
    def _exists(path):
        try:
            os.stat(path)
            return True
        except Exception:
            return False

    @staticmethod
    def _resolve_kmodel_path(configured_path):
        candidates = [configured_path, paths.KMODEL_PATH, paths.MODELS_DIR + "/best.kmodel"]
        for p in paths.KMODEL_FALLBACK_PATHS:
            candidates.append(p)

        seen = []
        for p in candidates:
            if p in seen:
                continue
            seen.append(p)
            if VictimDetector._exists(p):
                if p != configured_path:
                    print("detector: using fallback kmodel path", p)
                return p

        print("detector: kmodel not found. Checked:")
        for p in seen:
            print("  ", p)
        VictimDetector._print_dir_hint("/data")
        VictimDetector._print_dir_hint("/data/models")
        VictimDetector._print_dir_hint("/sdcard")
        VictimDetector._print_dir_hint("/sdcard/data/models")
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
    def _print_dir_hint(path):
        try:
            print("detector: ls {} -> {}".format(path, os.listdir(path)))
        except Exception as e:
            print("detector: ls {} FAILED ({})".format(path, e))

    @staticmethod
    def _chw_from_grayscale(img):
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


Detector = VictimDetector

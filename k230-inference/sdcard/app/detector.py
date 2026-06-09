# app/detector.py -- YOLOv8 anchor-free inference on the K230 KPU.
#
# Public API:
#   Detector(sensor) -> instance      load kmodel + build ai2d
#   .infer(img)      -> list of boxes [cls, score, x1, y1,x2, y2] in sensor coords
#   .release()                        deinit kpu / ai2d
#
# Reads /data/models/deploy_config.json to find the kmodel + class list.

import ujson

import nncase_runtime as nn
import ulab.numpy as np

import config
import yolov8_decode as yd


class Detector:
    def __init__(self, sensor):
        cfg = self._load_deploy_config()
        self._cfg          = cfg
        self.labels        = cfg["categories"]
        self.num_classes   = cfg["num_classes"]
        self.img_size      = cfg["img_size"]            # [W, H]
        self._kmodel_path  = cfg["kmodel_path"]

        self.sensor_w = sensor.width()
        self.sensor_h = sensor.height()
        self.model_w  = self.img_size[0]
        self.model_h  = self.img_size[1]
        print("detector: sensor {}x{} -> model {}x{}".format(
            self.sensor_w, self.sensor_h, self.model_w, self.model_h))

        # KPU
        self._kpu = nn.kpu()
        self._kpu.load_kmodel(self._kmodel_path)

        # ai2d (letterbox 114 fill)
        self._ai2d = nn.ai2d()
        self._ai2d.set_dtype(nn.ai2d_format.NCHW_FMT, nn.ai2d_format.NCHW_FMT,
                              np.uint8, np.uint8)
        self.ratio = min(self.model_w / self.sensor_w,
                          self.model_h / self.sensor_h)
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
            results[0], self.num_classes, config.CONF_THRESHOLD,
            num_anchors=config.NUM_ANCHORS_640x480)
        boxes = yd.nms_class_wise(boxes, config.NMS_THRESHOLD)

        det = []
        for b in boxes:
            xyxy = yd.unletterbox_box(b[2:6], self.ratio, self.left, self.top,
                                       self.sensor_w, self.sensor_h)
            det.append([b[0], b[1], xyxy[0], xyxy[1], xyxy[2], xyxy[3]])
        return det

    # ------------------------------------------------------------------
    def release(self):
        if hasattr(self._kpu, "deinit"):
            try: self._kpu.deinit()
            except Exception: pass

    # ------------------------------------------------------------------
    @staticmethod
    def _load_deploy_config():
        path = config.DEPLOY_CONFIG_PATH
        with open(path, "r") as f:
            cfg = ujson.load(f)
        # Resolve kmodel_path relative to the deploy_config's directory.
        if not cfg["kmodel_path"].startswith("/"):
            cfg["kmodel_path"] = path.rsplit("/", 1)[0] + "/" + cfg["kmodel_path"]
        print("detector: loaded deploy_config", path)
        print("  kmodel    :", cfg["kmodel_path"])
        print("  img_size  :", cfg["img_size"])
        print("  categories:", cfg["categories"])
        print("  model_type:", cfg["model_type"])
        if cfg.get("model_type") != "AnchorFreeDet":
            raise ValueError("detector requires model_type=AnchorFreeDet")
        return cfg

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

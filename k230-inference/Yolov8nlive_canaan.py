# Yolov8nlive_canaan.py
# Live anchor-based detection on K230, using the Canaan-recommended class
# hierarchy (PipeLine + AIBase + Ai2d + aicube postprocess).
#
# Adapted from Canaan's insect_det example, with parameters set for your
# AI Cube-trained AnchorBaseDet model (silver/black detector).

from libs.PipeLine import PipeLine
from libs.AIBase import AIBase
from libs.AI2D import Ai2d
from libs.Utils import *
import os, sys, gc
from media.media import *
import nncase_runtime as nn
import ulab.numpy as np
import image
import aicube


# ---------------------------------------------------------------------------
# Parameters from your deploy_config.json
# ---------------------------------------------------------------------------
KMODEL_PATH = "/data/kmodel/best_AnchorBaseDet_can3_5_n_20260514232500.kmodel"

# Labels: deploy_config.json says ["black", "silver"] but based on K230 testing
# the model's class index 0 actually corresponds to silver, not black. Try both
# orders if results look swapped.
LABELS = ["silver", "black"]    # if wrong, switch to ["black", "silver"]

# Anchors from deploy_config.json: 3 strides x 3 anchors x (w, h) = 18 values
ANCHORS = [
    33, 43,  41, 56,  55,  63,    # stride 8
    59, 81,  72, 92,  87, 111,    # stride 16
    103,134, 161,121, 134,157,    # stride 32
]

MODEL_INPUT_SIZE     = [640, 480]   # [W, H] — must match training input size
RGB888P_SIZE         = [640, 480]   # camera frame size
DISPLAY_MODE         = "lcd"

# Detection thresholds (deploy_config.json had 0.5/0.5; use lower for debugging)
CONFIDENCE_THRESHOLD = 0.3
NMS_THRESHOLD        = 0.5
NMS_OPTION           = False

MODEL_TYPE = "AnchorBaseDet"
STRIDES    = [8, 16, 32]


class DetectionApp(AIBase):
    def __init__(self, kmodel_path, labels, model_input_size, anchors,
                 model_type, confidence_threshold, nms_threshold, nms_option,
                 strides, rgb888p_size, display_size, debug_mode):
        super().__init__(kmodel_path, model_input_size, rgb888p_size, debug_mode)
        self.kmodel_path = kmodel_path
        self.labels = labels
        self.model_input_size = model_input_size
        self.anchors = anchors
        self.model_type = model_type
        self.confidence_threshold = confidence_threshold
        self.nms_threshold = nms_threshold
        self.nms_option = nms_option
        self.strides = strides
        self.rgb888p_size = [ALIGN_UP(rgb888p_size[0], 16), rgb888p_size[1]]
        self.display_size = [ALIGN_UP(display_size[0], 16), display_size[1]]
        self.debug_mode = debug_mode
        self.color_four = get_colors(len(self.labels))
        self.ai2d = Ai2d(debug_mode)
        self.ai2d.set_ai2d_dtype(nn.ai2d_format.NCHW_FMT,
                                 nn.ai2d_format.NCHW_FMT,
                                 np.uint8, np.uint8)

    def config_preprocess(self, input_image_size=None):
        with ScopedTiming("set preprocess config", self.debug_mode > 0):
            ai2d_input_size = input_image_size if input_image_size else self.rgb888p_size
            top, bottom, left, right, _ = center_pad_param(self.rgb888p_size, self.model_input_size)
            self.ai2d.pad([0, 0, 0, 0, top, bottom, left, right], 0, [114, 114, 114])
            self.ai2d.resize(nn.interp_method.tf_bilinear, nn.interp_mode.half_pixel)
            self.ai2d.build([1, 3, ai2d_input_size[1], ai2d_input_size[0]],
                            [1, 3, self.model_input_size[1], self.model_input_size[0]])

    def postprocess(self, results):
        with ScopedTiming("postprocess", self.debug_mode > 0):
            if self.model_type == "AnchorBaseDet":
                return aicube.anchorbasedet_post_process(
                    results[0], results[1], results[2],
                    self.model_input_size, self.rgb888p_size, self.strides,
                    len(self.labels), self.confidence_threshold,
                    self.nms_threshold, self.anchors, self.nms_option)
            elif self.model_type == "GFLDet":
                return aicube.gfldet_post_process(
                    results[0], results[1], results[2],
                    self.model_input_size, self.rgb888p_size, self.strides,
                    len(self.labels), self.confidence_threshold,
                    self.nms_threshold, self.nms_option)
            elif self.model_type == "AnchorFreeDet":
                return aicube.anchorfreedet_post_process(
                    results[0], results[1], results[2],
                    self.model_input_size, self.rgb888p_size, self.strides,
                    len(self.labels), self.confidence_threshold,
                    self.nms_threshold, self.nms_option)
            return None

    def draw_result(self, pl, det_boxes):
        with ScopedTiming("draw osd", self.debug_mode > 0):
            pl.osd_img.clear()
            if not det_boxes:
                return
            for b in det_boxes:
                cls, score, x1, y1, x2, y2 = b[0], b[1], b[2], b[3], b[4], b[5]
                sx = int(x1 * self.display_size[0] // self.rgb888p_size[0])
                sy = int(y1 * self.display_size[1] // self.rgb888p_size[1])
                w  = int((x2 - x1) * self.display_size[0] // self.rgb888p_size[0])
                h  = int((y2 - y1) * self.display_size[1] // self.rgb888p_size[1])
                color = self.color_four[cls % len(self.color_four)]
                pl.osd_img.draw_rectangle(sx, sy, w, h, color=color, thickness=2)
                pl.osd_img.draw_string_advanced(
                    sx, max(0, sy - 32), 28,
                    "{} {:.2f}".format(self.labels[cls], score),
                    color=color)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
if __name__ == "__main__":
    pl = PipeLine(rgb888p_size=RGB888P_SIZE, display_mode=DISPLAY_MODE)
    pl.create()
    display_size = pl.get_display_size()

    det = DetectionApp(
        kmodel_path=KMODEL_PATH,
        labels=LABELS,
        model_input_size=MODEL_INPUT_SIZE,
        anchors=ANCHORS,
        model_type=MODEL_TYPE,
        confidence_threshold=CONFIDENCE_THRESHOLD,
        nms_threshold=NMS_THRESHOLD,
        nms_option=NMS_OPTION,
        strides=STRIDES,
        rgb888p_size=RGB888P_SIZE,
        display_size=display_size,
        debug_mode=0,
    )
    det.config_preprocess()

    print("running. Ctrl+C to stop.")
    try:
        while True:
            with ScopedTiming("total", 1):
                img = pl.get_frame()
                det_boxes = det.run(img)
                det.draw_result(pl, det_boxes)
                pl.show_image()
                gc.collect()
    except KeyboardInterrupt:
        pass
    finally:
        det.deinit()
        pl.destroy()

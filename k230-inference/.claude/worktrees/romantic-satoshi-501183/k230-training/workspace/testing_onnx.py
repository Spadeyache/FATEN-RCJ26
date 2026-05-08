"""
Test ONNX model (e.g. best_640x480.onnx) with a single image.
Designed to run in Docker (no GUI); writes result image to file with boxes overlayed.
Resizes explicitly to the model's trained input size (height/width are configurable).
"""
import os
import sys
import cv2
import numpy as np
import onnxruntime as ort

# Default paths (work from workspace/ in repo or in Docker with mounted volumes)
DEFAULT_MODEL = os.path.join(os.path.dirname(__file__), "..", "models", "best_640x480.onnx")
DATA_YAML = os.path.join(os.path.dirname(__file__), "data.yaml")
# Training images: try Docker path first, then repo-relative
DEFAULT_IMAGE_DIRS = [
    "/datasets/my_dataset/train/images",
    "/datasets/my_dataset/val/images",
    "/datasets/my_dataset/test/images",
    os.path.join(os.path.dirname(__file__), "..", "datasets", "my_dataset", "train", "images"),
    os.path.join(os.path.dirname(__file__), "..", "datasets", "my_dataset", "val", "images"),
    os.path.join(os.path.dirname(__file__), "..", "datasets", "my_dataset", "test", "images"),
]

DEFAULT_IMG_HEIGHT = 480
DEFAULT_IMG_WIDTH = 640
CONF_THRESH = 0.25
IOU_THRESH = 0.45

# Box colors BGR (one per class, cycle if more classes)
BOX_COLORS = [
    (0, 0, 255),    # Red
    (0, 255, 0),    # Green
    (255, 0, 0),    # Blue
    (255, 255, 0),  # Cyan
    (255, 0, 255),  # Magenta
    (0, 255, 255),  # Yellow
]


def _load_class_names(yaml_path=None):
    """Load class names from data.yaml; fallback to class_0, class_1, ..."""
    names = []
    path = yaml_path or DATA_YAML
    if os.path.isfile(path):
        try:
            import yaml
            with open(path, "r") as f:
                cfg = yaml.safe_load(f)
            nc = cfg.get("nc", 0)
            n = cfg.get("names")
            if isinstance(n, dict):
                names = [n.get(i, f"class_{i}") for i in range(nc)]
            elif isinstance(n, (list, tuple)):
                names = list(n)[:nc]
            else:
                names = [f"class_{i}" for i in range(nc)]
        except Exception:
            pass
    if not names:
        names = ["class_0", "class_1"]  # will be extended below from output shape
    return names


# def _postprocess_yolo(output, conf_thresh, iou_thresh, img_height, img_width):
    # """
    # YOLOv8 ONNX output: (1, 4+nc, num_boxes). Match Ultralytics: transpose(squeeze(...)).
    # Returns list of (x1, y1, x2, y2, conf, cls_id) in pixel coords.
    # """
def _postprocess_yolo(output, conf_thresh, iou_thresh, img_height, img_width):
    raw = np.squeeze(output[0])  # (6, 6300)
    pred = raw.T                  # (6300, 6)  -> (xc, yc, w, h, score0, score1)

    detections = []
    for i in range(pred.shape[0]):
        xc, yc, w, h = pred[i, :4]
        scores = pred[i, 4:]

        conf = float(np.max(scores))
        if conf < conf_thresh:
            continue
        cls_id = int(np.argmax(scores))

        # Coordinates are already in pixel space (not normalized)
        x1 = int(xc - w / 2)
        y1 = int(yc - h / 2)
        x2 = int(xc + w / 2)
        y2 = int(yc + h / 2)
        x1 = max(0, min(x1, img_width - 1))
        y1 = max(0, min(y1, img_height - 1))
        x2 = max(0, min(x2, img_width))
        y2 = max(0, min(y2, img_height))
        detections.append((x1, y1, x2, y2, conf, cls_id))

    return _nms(detections, iou_thresh)

def _nms(detections, iou_threshold):
    """Non-maximum suppression. detections: list of (x1, y1, x2, y2, conf, cls_id)."""
    if not detections:
        return []
    detections = sorted(detections, key=lambda x: x[4], reverse=True)
    keep = []
    while detections:
        best = detections.pop(0)
        keep.append(best)
        x1_b, y1_b, x2_b, y2_b, _, c_b = best
        area_b = (x2_b - x1_b) * (y2_b - y1_b)
        def iou(other):
            x1, y1, x2, y2 = other[0], other[1], other[2], other[3]
            xi1, yi1 = max(x1_b, x1), max(y1_b, y1)
            xi2, yi2 = min(x2_b, x2), min(y2_b, y2)
            inter = max(0, xi2 - xi1) * max(0, yi2 - yi1)
            area = (x2 - x1) * (y2 - y1)
            return inter / (area_b + area - inter) if (area_b + area - inter) > 0 else 0
        detections = [d for d in detections if d[5] != c_b or iou(d) < iou_threshold]
    return keep


def _draw_detections(img, detections, class_names):
    """Draw boxes and labels on img (BGR)."""
    for (x1, y1, x2, y2, conf, cls_id) in detections:
        color = BOX_COLORS[cls_id % len(BOX_COLORS)]
        name = class_names[cls_id] if cls_id < len(class_names) else f"class_{cls_id}"
        label = f"{name} {conf:.2f}"
        cv2.rectangle(img, (x1, y1), (x2, y2), color, 2)
        (tw, th), _ = cv2.getTextSize(label, cv2.FONT_HERSHEY_SIMPLEX, 0.5, 1)
        cv2.rectangle(img, (x1, y1 - th - 6), (x1 + tw, y1), color, -1)
        cv2.putText(img, label, (x1, y1 - 4), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1)
    return img


def _find_default_image():
    """Return path to first image found in training/val/test dirs."""
    exts = (".jpg", ".jpeg", ".png", ".bmp")
    for d in DEFAULT_IMAGE_DIRS:
        if not os.path.isdir(d):
            continue
        for f in sorted(os.listdir(d)):
            if f.lower().endswith(exts):
                return os.path.join(d, f)
    return None


def test_onnx_image(
    model_path,
    image_path,
    output_path=None,
    conf_thresh=None,
    img_height=DEFAULT_IMG_HEIGHT,
    img_width=DEFAULT_IMG_WIDTH,
):
    if conf_thresh is None:
        conf_thresh = CONF_THRESH
    if output_path is None:
        output_path = f"result_{img_width}x{img_height}.jpg"
    # 1. Load the ONNX model (CPU for Docker compatibility)
    session = ort.InferenceSession(model_path, providers=["CPUExecutionProvider"])
    input_name = session.get_inputs()[0].name

    # 2. Load and preprocess the image
    original_img = cv2.imread(image_path)
    if original_img is None:
        print("Error: Could not load image:", image_path)
        return

    # Resize to the explicit trained input size (width, height)
    resized_img = cv2.resize(original_img, (img_width, img_height))

    # Save resized input for inspection
    resized_path = f"resized_{img_width}x{img_height}.jpg"
    cv2.imwrite(resized_path, resized_img)
    print("Resized image saved as", resized_path)

    # BGR -> RGB, HWC -> CHW, normalize [0, 1]
    image_data = cv2.cvtColor(resized_img, cv2.COLOR_BGR2RGB)
    image_data = image_data.transpose(2, 0, 1)
    image_data = np.expand_dims(image_data, axis=0).astype(np.float32)
    image_data /= 255.0

    # 3. Inference
    outputs = session.run(None, {input_name: image_data})
    print("Inference successful. Output shapes:", [o.shape for o in outputs])

    out = outputs[0]
    raw_max = float(np.max(out))
    print("Raw output max score: {:.4f} (conf threshold: {})".format(raw_max, conf_thresh))


    # # ===== DEBUG =====
    # raw = np.squeeze(out)
    # print(f"Squeezed shape: {raw.shape}")
    # if raw.shape[0] < raw.shape[1]:
    #     pred = raw.T
    # else:
    #     pred = raw
    # print(f"Pred shape: {pred.shape}")
    # print(f"Sample coords: xc={pred[0,0]:.3f} yc={pred[0,1]:.3f} w={pred[0,2]:.3f} h={pred[0,3]:.3f}")
    # print(f"Score range: min={pred[:,4:].min():.4f} max={pred[:,4:].max():.4f} mean={pred[:,4:].mean():.4f}")
    # max_scores = pred[:, 4:].max(axis=1)
    # for thresh in [0.9, 0.7, 0.5, 0.25, 0.1, 0.05, 0.01]:
    #     print(f"  Boxes > {thresh}: {(max_scores > thresh).sum()}")
    # # ===== END DEBUG =====

    # 4. Post-process and overlay detections
    class_names = _load_class_names()
    nc = out.shape[1] - 4 if out.shape[0] <= out.shape[1] else out.shape[2] - 4
    while len(class_names) < nc:
        class_names.append(f"class_{len(class_names)}")

    detections = _postprocess_yolo(outputs, conf_thresh, IOU_THRESH, img_height, img_width)
    print("Detections: {}".format(len(detections)))
    for (x1, y1, x2, y2, conf, cls_id) in detections:
        name = class_names[cls_id] if cls_id < len(class_names) else f"class_{cls_id}"
        print("  {} {:.2f} @ ({},{})-({},{})".format(name, conf, x1, y1, x2, y2))

    if len(detections) == 0:
        print("No boxes above threshold. Try: python testing_onnx.py ... --conf 0.1")

    vis_img = resized_img.copy()
    _draw_detections(vis_img, detections, class_names)

    # 5. Save result with boxes overlayed (no GUI in Docker)
    cv2.imwrite(output_path, vis_img)
    abs_path = os.path.abspath(output_path)
    print("Result saved as: {}".format(abs_path))


def main():
    argv = sys.argv[1:]
    # Parse --conf, --output, --img-height, --img-width
    conf_thresh = None
    output_path = None
    img_height = DEFAULT_IMG_HEIGHT
    img_width = DEFAULT_IMG_WIDTH
    args = []
    i = 0
    while i < len(argv):
        if argv[i] == "--conf" and i + 1 < len(argv):
            try:
                conf_thresh = float(argv[i + 1])
            except ValueError:
                pass
            i += 2
            continue
        if argv[i] == "--output" and i + 1 < len(argv):
            output_path = argv[i + 1]
            i += 2
            continue
        if argv[i] == "--img-height" and i + 1 < len(argv):
            try:
                img_height = int(argv[i + 1])
            except ValueError:
                pass
            i += 2
            continue
        if argv[i] == "--img-width" and i + 1 < len(argv):
            try:
                img_width = int(argv[i + 1])
            except ValueError:
                pass
            i += 2
            continue
        if not argv[i].startswith("-"):
            args.append(argv[i])
        i += 1

    model_path = args[0] if len(args) >= 1 else DEFAULT_MODEL
    image_path = args[1] if len(args) >= 2 else None
    if len(args) >= 3 and output_path is None:
        output_path = args[2]

    if image_path is None:
        image_path = _find_default_image()
    if not image_path or not os.path.isfile(image_path):
        print("No image specified and no training image found.")
        print("Searched:", DEFAULT_IMAGE_DIRS)
        print(
            "Usage: python testing_onnx.py [model.onnx] [image.jpg] [out.jpg] "
            "[--conf 0.1] [--output path] [--img-height H] [--img-width W]"
        )
        sys.exit(1)
    if not os.path.isfile(model_path):
        print("Model not found:", model_path)
        sys.exit(1)

    print("Model:", model_path)
    print("Image:", image_path)
    print(f"Input size: {img_width}x{img_height}")
    test_onnx_image(
        model_path,
        image_path,
        output_path=output_path,
        conf_thresh=conf_thresh,
        img_height=img_height,
        img_width=img_width,
    )


if __name__ == "__main__":
    main()

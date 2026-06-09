# lib/yolov8_decode.py
#
# Pure-Python YOLOv8 anchor-free decoder. No CanMV / nncase imports here,
# so it's testable in isolation.
#
# Why not aicube.anchorfreedet_post_process?
#   That helper expects three separate per-stride raw feature maps and
#   performs DFL + grid decoding itself. Our Ultralytics ONNX export ends
#   in Concat-of-decoded-boxes + Sigmoid-on-classes, producing a SINGLE
#   already-decoded tensor of shape (1, 4+nc, 6300) where channels 0..3
#   are cx,cy,w,h in MODEL letterbox pixel coords and channels 4..3+nc are
#   sigmoided class scores. Feeding that to aicube reads bbox channels as
#   scores and prints things like "silver 638.50".

NUM_ANCHORS_640x480 = 6300   # 80*60 + 40*30 + 20*15


def iou_xyxy(a, b):
    ix1 = a[0] if a[0] > b[0] else b[0]
    iy1 = a[1] if a[1] > b[1] else b[1]
    ix2 = a[2] if a[2] < b[2] else b[2]
    iy2 = a[3] if a[3] < b[3] else b[3]
    iw = ix2 - ix1
    ih = iy2 - iy1
    if iw <= 0 or ih <= 0:
        return 0.0
    inter = iw * ih
    aa = (a[2] - a[0]) * (a[3] - a[1])
    bb = (b[2] - b[0]) * (b[3] - b[1])
    union = aa + bb - inter
    if union <= 0:
        return 0.0
    return inter / union


def decode_anchorfree(flat_out, num_classes, conf_thr,
                      num_anchors=NUM_ANCHORS_640x480):
    """Decode the YOLOv8 single concatenated output (1, 4+nc, N).

    flat_out is channel-major: flat[c * N + n] = channel c, anchor n.
    Returns list of [cls, score, x1, y1, x2, y2] in MODEL letterbox coords.
    """
    N = num_anchors
    off_cx = 0
    off_cy = N
    off_w  = 2 * N
    off_h  = 3 * N
    off_c  = 4 * N

    boxes = []
    for n in range(N):
        best_c = 0
        best_s = flat_out[off_c + n]
        for c in range(1, num_classes):
            s = flat_out[off_c + c * N + n]
            if s > best_s:
                best_s = s
                best_c = c
        if best_s < conf_thr:
            continue
        cx = flat_out[off_cx + n]
        cy = flat_out[off_cy + n]
        bw = flat_out[off_w  + n]
        bh = flat_out[off_h  + n]
        hw = bw / 2
        hh = bh / 2
        boxes.append([best_c, float(best_s),
                      float(cx - hw), float(cy - hh),
                      float(cx + hw), float(cy + hh)])
    return boxes


def nms_class_wise(boxes, iou_thr):
    """Per-class greedy NMS. Returns sorted-by-score list."""
    kept = []
    classes = set()
    for b in boxes:
        classes.add(b[0])
    for c in classes:
        cb = [b for b in boxes if b[0] == c]
        cb.sort(key=lambda b: -b[1])
        while cb:
            head = cb.pop(0)
            kept.append(head)
            cb = [b for b in cb if iou_xyxy(head[2:6], b[2:6]) < iou_thr]
    kept.sort(key=lambda b: -b[1])
    return kept


def unletterbox_box(box_xyxy, ratio, left_pad, top_pad, ori_w, ori_h):
    """Map a MODEL letterbox box back to SENSOR coords."""
    x1 = (box_xyxy[0] - left_pad) / ratio
    y1 = (box_xyxy[1] - top_pad)  / ratio
    x2 = (box_xyxy[2] - left_pad) / ratio
    y2 = (box_xyxy[3] - top_pad)  / ratio
    if x1 < 0: x1 = 0.0
    if y1 < 0: y1 = 0.0
    if x2 > ori_w: x2 = float(ori_w)
    if y2 > ori_h: y2 = float(ori_h)
    return [x1, y1, x2, y2]

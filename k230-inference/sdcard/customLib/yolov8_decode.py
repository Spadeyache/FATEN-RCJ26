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

# Vectorized decode needs ulab (on CanMV) or numpy (host tests). If neither
# imports, decode_anchorfree() silently uses the pure-Python fallback below.
try:
    from ulab import numpy as _np
except ImportError:
    try:
        import numpy as _np
    except ImportError:
        _np = None


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

    Fast path: ulab-vectorized (per-class max + argsort), so Python only
    touches the handful of anchors above threshold instead of all 6300 x nc.
    The interpreted fallback is the FPS killer (~hundreds of ms/frame on
    the K230) -- it only runs if ulab is missing or an op is unsupported.
    """
    global _fast_warned
    if _np is not None:
        try:
            return _decode_fast(flat_out, num_classes, conf_thr, num_anchors)
        except Exception as e:
            if not _fast_warned:
                # Loud on purpose: the fallback costs ~100x. If you see this,
                # the ulab build lacks an op (or flat_out isn't an ndarray).
                print("decode: FAST PATH FAILED -> python fallback:", repr(e))
                _fast_warned = True
    elif not _fast_warned:
        print("decode: no ulab/numpy -> python fallback (SLOW)")
        _fast_warned = True
    return _decode_python(flat_out, num_classes, conf_thr, num_anchors)


_fast_warned = False


_MAX_FAST_BOXES = 100   # safety cap; NMS cost explodes past this anyway


def _decode_fast(flat, nc, conf_thr, N):
    """Vectorized decode. `flat` must be a ulab/numpy 1-D array.

    No argsort: CanMV's ulab raises NotImplementedError for 1-D argsort
    ("flattened arrays"). Instead we pull detections out best-first with one
    C-speed argmax per box above threshold, suppressing each hit. Real frames
    have a handful of boxes, so this is ~10 argmax calls over N floats.
    """
    scores = flat[4 * N:(4 + nc) * N].reshape((nc, N))
    best_s = _np.max(scores, axis=0)        # (N,) best class score per anchor
    best_c = _np.argmax(scores, axis=0)     # (N,) that class id

    boxes = []
    while len(boxes) < _MAX_FAST_BOXES:
        n = int(_np.argmax(best_s))         # highest remaining score
        s = float(best_s[n])
        if s < conf_thr:
            break                           # everything left is below threshold
        cx = float(flat[n])
        cy = float(flat[N + n])
        hw = float(flat[2 * N + n]) / 2
        hh = float(flat[3 * N + n]) / 2
        boxes.append([int(best_c[n]), s,
                      cx - hw, cy - hh, cx + hw, cy + hh])
        best_s[n] = -1.0                    # suppress; find the next best
    return boxes


def _decode_python(flat_out, num_classes, conf_thr, num_anchors):
    """Pure-Python fallback. Same output, ~100x slower on device."""
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


# Import-time self-test on a tiny tensor so the boot log states which decode
# path this firmware will actually run (shows up before the first frame).
if _np is not None:
    try:
        _decode_fast(_np.zeros(((4 + 2) * 4,), dtype=_np.float32), 2, 0.5, 4)
        print("decode: ulab fast path OK")
    except Exception as _e:
        print("decode: fast path UNAVAILABLE ({}) -> python fallback (SLOW)".format(repr(_e)))
else:
    print("decode: no ulab/numpy -> python fallback (SLOW)")


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

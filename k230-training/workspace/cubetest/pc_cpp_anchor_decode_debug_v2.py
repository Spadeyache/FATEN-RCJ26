# pc_cpp_anchor_decode_debug_v2.py
# Debug version: proves whether the C++-style decoder is actually seeing scores > threshold.
#
# It prints:
#   - max decoded score per output scale
#   - top raw decoded records before threshold filtering
#   - boxes before/after NMS
#
# Run:
# python3 pc_cpp_anchor_decode_debug_v2.py \
#   --kmodel /workspace/cubetest/best_AnchorBaseDet_can3_5_n_20260514232500.kmodel \
#   --image /workspace/cubetest/1s0b_0010.jpg \
#   --threshold 0.2 \
#   --out /workspace/cubetest/cpp_decode_debug_v2.jpg

import argparse
import cv2
import numpy as np
import nncase as nc

LABELS = ["black", "silver"]
STRIDES = [8, 16, 32]
ANCHORS = [
    [(33, 43), (41, 56), (55, 63)],
    [(59, 81), (72, 92), (87, 111)],
    [(103, 134), (161, 121), (134, 157)],
]


def load_input(image_path, width, height):
    bgr = cv2.imread(image_path, cv2.IMREAD_COLOR)
    if bgr is None:
        raise FileNotFoundError(image_path)
    bgr_resized = cv2.resize(bgr, (width, height), interpolation=cv2.INTER_LINEAR)
    rgb = cv2.cvtColor(bgr_resized, cv2.COLOR_BGR2RGB)
    inp = rgb.transpose(2, 0, 1)[None].astype(np.uint8)
    return bgr_resized, inp


def run_kmodel(kmodel_path, inp, try_outputs=3):
    sim = nc.Simulator()
    sim.load_model(open(kmodel_path, "rb").read())
    sim.set_input_tensor(0, nc.RuntimeTensor.from_numpy(inp))
    sim.run()

    outs = []
    for i in range(try_outputs):
        try:
            outs.append(sim.get_output_tensor(i).to_numpy())
        except Exception as e:
            print(f"output[{i}] failed: {repr(e)}")
    return outs


def iou(a, b):
    x1 = max(a[2], b[2])
    y1 = max(a[3], b[3])
    x2 = min(a[4], b[4])
    y2 = min(a[5], b[5])
    w = max(0.0, x2 - x1 + 1)
    h = max(0.0, y2 - y1 + 1)
    inter = w * h
    area_a = (a[4] - a[2] + 1) * (a[5] - a[3] + 1)
    area_b = (b[4] - b[2] + 1) * (b[5] - b[3] + 1)
    return inter / (area_a + area_b - inter + 1e-9)


def nms_classwise(boxes, nms_thresh):
    out = []
    for cls in sorted(set(int(b[0]) for b in boxes)):
        cls_boxes = [b for b in boxes if int(b[0]) == cls]
        cls_boxes.sort(key=lambda x: -x[1])
        while cls_boxes:
            best = cls_boxes.pop(0)
            out.append(best)
            cls_boxes = [b for b in cls_boxes if iou(best, b) < nms_thresh]
    out.sort(key=lambda x: -x[1])
    return out


def decode_one_output(out, scale_index, model_w, model_h, frame_w, frame_h, threshold, topk_debug=10):
    stride = STRIDES[scale_index]
    anchors = ANCHORS[scale_index]
    h_grid = out.shape[1]
    w_grid = out.shape[2]
    num_classes = len(LABELS)
    one_rsize = num_classes + 5

    # Direct NHWC -> (H,W,3,7). This should match C++ record=(loc*3+i)*7.
    y = out[0].reshape(h_grid, w_grid, 3, one_rsize)

    ratiow = model_w / frame_w
    ratioh = model_h / frame_h
    gain = min(ratiow, ratioh)

    boxes = []
    top = []

    for gy in range(h_grid):
        for gx in range(w_grid):
            for ai in range(3):
                rec = y[gy, gx, ai]
                x_raw = float(rec[0])
                y_raw = float(rec[1])
                w_raw = float(rec[2])
                h_raw = float(rec[3])
                obj = float(rec[4])

                for cls in range(num_classes):
                    cls_score = float(rec[5 + cls])
                    score = cls_score * obj

                    top.append((score, scale_index, gy, gx, ai, cls, obj, cls_score, x_raw, y_raw, w_raw, h_raw))

                    if score <= threshold:
                        continue

                    cx = ((x_raw * 2.0) - 0.5 + gx) * stride
                    cy = ((y_raw * 2.0) - 0.5 + gy) * stride
                    bw = ((w_raw * 2.0) ** 2) * anchors[ai][0]
                    bh = ((h_raw * 2.0) ** 2) * anchors[ai][1]

                    cx -= ((model_w - frame_w * gain) / 2.0)
                    cy -= ((model_h - frame_h * gain) / 2.0)
                    cx /= gain
                    cy /= gain
                    bw /= gain
                    bh /= gain

                    x1 = max(0, min(frame_w, int(cx - bw / 2.0)))
                    y1 = max(0, min(frame_h, int(cy - bh / 2.0)))
                    x2 = max(0, min(frame_w, int(cx + bw / 2.0)))
                    y2 = max(0, min(frame_h, int(cy + bh / 2.0)))

                    boxes.append([cls, score, x1, y1, x2, y2, scale_index, gy, gx, ai, obj, cls_score])

    top.sort(key=lambda x: -x[0])
    print(f"\nScale {scale_index} stride={stride}")
    print(f"  max decoded score = {top[0][0]:.6f}")
    print(f"  count score > {threshold} = {sum(1 for r in top if r[0] > threshold)}")
    print(f"  top {topk_debug} records before threshold:")
    for r in top[:topk_debug]:
        score, si, gy, gx, ai, cls, obj, cls_score, x_raw, y_raw, w_raw, h_raw = r
        print(
            f"    score={score:.6f} {LABELS[cls]} scale={si} cell=({gy},{gx}) anchor={ai} "
            f"obj={obj:.6f} cls={cls_score:.6f} raw_xywh=({x_raw:.6f},{y_raw:.6f},{w_raw:.6f},{h_raw:.6f})"
        )

    return boxes, top


def draw_boxes(img, boxes, out_path, max_draw=50):
    colors = [(0, 0, 255), (0, 255, 0), (255, 0, 0), (0, 255, 255)]
    draw = img.copy()

    for b in boxes[:max_draw]:
        cls, score, x1, y1, x2, y2 = b[:6]
        cls = int(cls)
        color = colors[cls % len(colors)]
        label = LABELS[cls] if 0 <= cls < len(LABELS) else f"c{cls}"
        cv2.rectangle(draw, (int(x1), int(y1)), (int(x2), int(y2)), color, 2)
        cv2.putText(draw, f"{label} {score:.2f}", (int(x1), max(20, int(y1) - 5)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.55, color, 2)

    cv2.imwrite(out_path, draw)
    print("Saved:", out_path)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--kmodel", required=True)
    p.add_argument("--image", required=True)
    p.add_argument("--threshold", type=float, default=0.2)
    p.add_argument("--nms", type=float, default=0.5)
    p.add_argument("--width", type=int, default=640)
    p.add_argument("--height", type=int, default=480)
    p.add_argument("--out", default="cpp_decode_debug_v2.jpg")
    args = p.parse_args()

    print("=== PC C++ AnchorBaseDet decoder debug v2 ===")
    print("kmodel:", args.kmodel)
    print("image :", args.image)
    print("threshold:", args.threshold)

    bgr, inp = load_input(args.image, args.width, args.height)
    print("input:", inp.shape, inp.dtype, inp.min(), inp.max(), inp.mean())
    print("input first 20:", inp.reshape(-1)[:20])

    outs = run_kmodel(args.kmodel, inp, 3)
    for i, out in enumerate(outs):
        print(f"output[{i}] shape={out.shape} min={out.min():.6f} max={out.max():.6f} mean={out.mean():.6f}")

    boxes = []
    for i, out in enumerate(outs):
        b, _ = decode_one_output(out, i, args.width, args.height, args.width, args.height, args.threshold)
        boxes.extend(b)

    print("\nboxes before NMS:", len(boxes))
    boxes = nms_classwise(boxes, args.nms)
    print("boxes after classwise NMS:", len(boxes))

    print("Top boxes:")
    for b in boxes[:30]:
        cls, score, x1, y1, x2, y2, scale_i, gy, gx, anchor_i, obj, cls_score = b
        print(
            f"  {LABELS[int(cls)]} {score:.6f} box=({x1},{y1})-({x2},{y2}) "
            f"scale={scale_i} cell=({gy},{gx}) anchor={anchor_i} obj={obj:.6f} cls={cls_score:.6f}"
        )

    draw_boxes(bgr, boxes, args.out)


if __name__ == "__main__":
    main()

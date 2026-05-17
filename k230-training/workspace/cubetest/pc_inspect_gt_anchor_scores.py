# pc_inspect_gt_anchor_scores.py
# Inspect final .kmodel outputs around the ground-truth YOLO label.
#
# Purpose:
#   Answer:
#     "For a train image with a silver object, does the final .kmodel produce
#      any strong object/class score at the actual labeled object location?"
#
# This does NOT rely only on global top detections.
# It checks:
#   1. Ground-truth box from YOLO txt label
#   2. Expected grid cell at stride 8/16/32
#   3. Scores near that cell for every anchor
#   4. Decoded candidate boxes near the ground-truth location
#
# Example:
# python3 pc_inspect_gt_anchor_scores.py \
#   --kmodel /workspace/cubetest/best_AnchorBaseDet_can3_5_n_20260514232500.kmodel \
#   --image /workspace/cubetest/1s0b_0010.jpg \
#   --label /datasets/my_dataset/train/labels/1s0b_0010.txt \
#   --out /workspace/cubetest/gt_anchor_inspect.jpg

import argparse
import os
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


def read_yolo_labels(label_path, img_w, img_h):
    boxes = []
    if not os.path.exists(label_path):
        raise FileNotFoundError(label_path)

    with open(label_path, "r") as f:
        for line in f:
            p = line.strip().split()
            if len(p) < 5:
                continue
            cls = int(float(p[0]))
            xc = float(p[1]) * img_w
            yc = float(p[2]) * img_h
            bw = float(p[3]) * img_w
            bh = float(p[4]) * img_h
            x1 = xc - bw / 2
            y1 = yc - bh / 2
            x2 = xc + bw / 2
            y2 = yc + bh / 2
            boxes.append({
                "cls": cls, "xc": xc, "yc": yc, "bw": bw, "bh": bh,
                "x1": x1, "y1": y1, "x2": x2, "y2": y2
            })
    return boxes


def run_kmodel(kmodel_path, inp):
    sim = nc.Simulator()
    sim.load_model(open(kmodel_path, "rb").read())
    sim.set_input_tensor(0, nc.RuntimeTensor.from_numpy(inp))
    sim.run()

    outs = []
    for i in range(3):
        outs.append(sim.get_output_tensor(i).to_numpy())
    return outs


def box_iou_xyxy(a, b):
    x1 = max(a[0], b[0])
    y1 = max(a[1], b[1])
    x2 = min(a[2], b[2])
    y2 = min(a[3], b[3])
    inter = max(0, x2 - x1) * max(0, y2 - y1)

    area_a = max(0, a[2] - a[0]) * max(0, a[3] - a[1])
    area_b = max(0, b[2] - b[0]) * max(0, b[3] - b[1])

    return inter / (area_a + area_b - inter + 1e-9)


def decode_record(rec, gx, gy, anchor, stride, model_w, model_h, frame_w, frame_h):
    x_raw, y_raw, w_raw, h_raw = map(float, rec[:4])

    gain = min(model_w / frame_w, model_h / frame_h)

    cx = ((x_raw * 2.0) - 0.5 + gx) * stride
    cy = ((y_raw * 2.0) - 0.5 + gy) * stride
    bw = ((w_raw * 2.0) ** 2) * anchor[0]
    bh = ((h_raw * 2.0) ** 2) * anchor[1]

    cx -= ((model_w - frame_w * gain) / 2.0)
    cy -= ((model_h - frame_h * gain) / 2.0)

    cx /= gain
    cy /= gain
    bw /= gain
    bh /= gain

    x1 = max(0, min(frame_w, cx - bw / 2.0))
    y1 = max(0, min(frame_h, cy - bh / 2.0))
    x2 = max(0, min(frame_w, cx + bw / 2.0))
    y2 = max(0, min(frame_h, cy + bh / 2.0))

    return x1, y1, x2, y2


def inspect_near_gt(outputs, gt, model_w, model_h, frame_w, frame_h, radius=2):
    gt_cls = gt["cls"]
    gt_name = LABELS[gt_cls] if 0 <= gt_cls < len(LABELS) else f"c{gt_cls}"
    gt_box = [gt["x1"], gt["y1"], gt["x2"], gt["y2"]]

    print("\n=================================================")
    print(f"GT {gt_name}: center=({gt['xc']:.1f},{gt['yc']:.1f}) size=({gt['bw']:.1f},{gt['bh']:.1f})")
    print(f"GT box=({gt['x1']:.1f},{gt['y1']:.1f})-({gt['x2']:.1f},{gt['y2']:.1f})")

    candidates = []

    for scale_i, out in enumerate(outputs):
        stride = STRIDES[scale_i]
        anchors = ANCHORS[scale_i]

        h_grid, w_grid = out.shape[1], out.shape[2]
        y = out[0].reshape(h_grid, w_grid, 3, 7)

        gx0 = int(gt["xc"] // stride)
        gy0 = int(gt["yc"] // stride)

        print(f"\nScale {scale_i} stride={stride} expected GT cell≈({gy0},{gx0}) grid={h_grid}x{w_grid}")

        y_min = max(0, gy0 - radius)
        y_max = min(h_grid - 1, gy0 + radius)
        x_min = max(0, gx0 - radius)
        x_max = min(w_grid - 1, gx0 + radius)

        local_records = []

        for gy in range(y_min, y_max + 1):
            for gx in range(x_min, x_max + 1):
                for ai in range(3):
                    rec = y[gy, gx, ai]
                    obj = float(rec[4])
                    c0 = float(rec[5])
                    c1 = float(rec[6])
                    cls_score = float(rec[5 + gt_cls])
                    score_gt_cls = obj * cls_score
                    pred_cls = 0 if c0 >= c1 else 1
                    pred_cls_score = max(c0, c1)
                    score_pred_cls = obj * pred_cls_score

                    box = decode_record(
                        rec, gx, gy, anchors[ai], stride,
                        model_w, model_h, frame_w, frame_h
                    )
                    iou = box_iou_xyxy(box, gt_box)

                    row = {
                        "scale": scale_i,
                        "stride": stride,
                        "gy": gy,
                        "gx": gx,
                        "anchor": ai,
                        "obj": obj,
                        "c0": c0,
                        "c1": c1,
                        "gt_score": score_gt_cls,
                        "pred_score": score_pred_cls,
                        "pred_cls": pred_cls,
                        "box": box,
                        "iou": iou,
                    }
                    local_records.append(row)
                    candidates.append(row)

        local_records.sort(key=lambda r: (-r["gt_score"], -r["iou"]))

        print("Top local records around GT cell by GT-class score:")
        for r in local_records[:12]:
            pred_name = LABELS[r["pred_cls"]]
            b = r["box"]
            print(
                f"  cell=({r['gy']},{r['gx']}) a={r['anchor']} "
                f"obj={r['obj']:.6f} c0={r['c0']:.6f} c1={r['c1']:.6f} "
                f"GTscore={r['gt_score']:.6f} Pred={pred_name}:{r['pred_score']:.6f} "
                f"IoU={r['iou']:.3f} box=({b[0]:.1f},{b[1]:.1f})-({b[2]:.1f},{b[3]:.1f})"
            )

    candidates.sort(key=lambda r: -r["iou"])
    print("\nBest decoded candidates by IoU with GT:")
    for r in candidates[:15]:
        pred_name = LABELS[r["pred_cls"]]
        b = r["box"]
        print(
            f"  IoU={r['iou']:.3f} scale={r['scale']} stride={r['stride']} "
            f"cell=({r['gy']},{r['gx']}) a={r['anchor']} "
            f"GTscore={r['gt_score']:.6f} Pred={pred_name}:{r['pred_score']:.6f} "
            f"obj={r['obj']:.6f} box=({b[0]:.1f},{b[1]:.1f})-({b[2]:.1f},{b[3]:.1f})"
        )

    return candidates


def draw_debug(img, gts, candidates, out_path):
    draw = img.copy()

    for gt in gts:
        cls = gt["cls"]
        name = LABELS[cls] if 0 <= cls < len(LABELS) else f"c{cls}"
        cv2.rectangle(draw, (int(gt["x1"]), int(gt["y1"])), (int(gt["x2"]), int(gt["y2"])), (0, 255, 0), 2)
        cv2.putText(draw, f"GT {name}", (int(gt["x1"]), max(20, int(gt["y1"]) - 5)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2)

    candidates = sorted(candidates, key=lambda r: -r["iou"])
    for i, r in enumerate(candidates[:10]):
        b = r["box"]
        color = (0, 0, 255) if i == 0 else (0, 255, 255)
        cv2.rectangle(draw, (int(b[0]), int(b[1])), (int(b[2]), int(b[3])), color, 1)
        cv2.putText(draw, f"cand{i+1} IoU{r['iou']:.2f} s{r['gt_score']:.2f}",
                    (int(b[0]), max(20, int(b[1]) - 5)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.45, color, 1)

    cv2.imwrite(out_path, draw)
    print("\nSaved debug image:", out_path)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--kmodel", required=True)
    p.add_argument("--image", required=True)
    p.add_argument("--label", required=True)
    p.add_argument("--width", type=int, default=640)
    p.add_argument("--height", type=int, default=480)
    p.add_argument("--radius", type=int, default=2)
    p.add_argument("--out", default="gt_anchor_inspect.jpg")
    args = p.parse_args()

    bgr, inp = load_input(args.image, args.width, args.height)
    outputs = run_kmodel(args.kmodel, inp)
    gts = read_yolo_labels(args.label, args.width, args.height)

    print("=== GT anchor score inspection ===")
    print("image:", args.image)
    print("label:", args.label)
    print("input:", inp.shape, inp.dtype, inp.min(), inp.max(), inp.mean())

    all_candidates = []
    for gt in gts:
        cands = inspect_near_gt(outputs, gt, args.width, args.height, args.width, args.height, radius=args.radius)
        all_candidates.extend(cands)

    draw_debug(bgr, gts, all_candidates, args.out)


if __name__ == "__main__":
    main()

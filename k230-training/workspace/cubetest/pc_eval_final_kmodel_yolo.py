# pc_eval_final_kmodel_yolo.py
# Evaluate the FINAL .kmodel on PC against YOLO-format labels using the C++ AnchorBaseDet decoder.
#
# This answers:
#   "Does the deployed .kmodel actually detect objects in the dataset images?"
#
# It does NOT use AI Cube GUI.
# It uses:
#   JPG image -> OpenCV preprocess -> final .kmodel -> C++-style decoder -> IoU with YOLO labels
#
# Example:
# python3 pc_eval_final_kmodel_yolo.py \
#   --kmodel /workspace/cubetest/best_AnchorBaseDet_can3_5_n_20260514232500.kmodel \
#   --images-dir /datasets/my_dataset/train/images \
#   --labels-dir /datasets/my_dataset/train/labels \
#   --limit 30 \
#   --out-csv /workspace/cubetest/final_kmodel_eval.csv

import argparse
import csv
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

THRESHOLDS = [0.003, 0.005, 0.01, 0.03, 0.05, 0.10, 0.20, 0.40]


def image_to_tensor(image_path, width, height):
    bgr = cv2.imread(image_path, cv2.IMREAD_COLOR)
    if bgr is None:
        raise FileNotFoundError(image_path)
    bgr = cv2.resize(bgr, (width, height), interpolation=cv2.INTER_LINEAR)
    rgb = cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)
    inp = rgb.transpose(2, 0, 1)[None].astype(np.uint8)
    return bgr, inp


def read_yolo_label(label_path, width, height):
    gts = []
    if not os.path.exists(label_path):
        return gts

    with open(label_path, "r") as f:
        for line in f:
            p = line.strip().split()
            if len(p) < 5:
                continue
            cls = int(float(p[0]))
            xc = float(p[1]) * width
            yc = float(p[2]) * height
            bw = float(p[3]) * width
            bh = float(p[4]) * height
            gts.append({
                "cls": cls,
                "xc": xc,
                "yc": yc,
                "bw": bw,
                "bh": bh,
                "x1": xc - bw / 2,
                "y1": yc - bh / 2,
                "x2": xc + bw / 2,
                "y2": yc + bh / 2,
            })
    return gts


def iou_xyxy(a, b):
    x1 = max(a[0], b[0])
    y1 = max(a[1], b[1])
    x2 = min(a[2], b[2])
    y2 = min(a[3], b[3])

    inter = max(0, x2 - x1) * max(0, y2 - y1)
    area_a = max(0, a[2] - a[0]) * max(0, a[3] - a[1])
    area_b = max(0, b[2] - b[0]) * max(0, b[3] - b[1])
    return inter / (area_a + area_b - inter + 1e-9)


def decode_record(rec, gx, gy, anchor, stride, model_w, model_h, frame_w, frame_h):
    x_raw = float(rec[0])
    y_raw = float(rec[1])
    w_raw = float(rec[2])
    h_raw = float(rec[3])

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

    x1 = max(0, min(frame_w, cx - bw / 2))
    y1 = max(0, min(frame_h, cy - bh / 2))
    x2 = max(0, min(frame_w, cx + bw / 2))
    y2 = max(0, min(frame_h, cy + bh / 2))

    return [x1, y1, x2, y2]


def decode_outputs(outputs, width, height):
    boxes = []

    for scale_i, out in enumerate(outputs):
        stride = STRIDES[scale_i]
        anchors = ANCHORS[scale_i]
        h_grid = out.shape[1]
        w_grid = out.shape[2]
        y = out[0].reshape(h_grid, w_grid, 3, 7)

        for gy in range(h_grid):
            for gx in range(w_grid):
                for ai in range(3):
                    rec = y[gy, gx, ai]
                    obj = float(rec[4])

                    for cls in range(len(LABELS)):
                        cls_score = float(rec[5 + cls])
                        score = obj * cls_score
                        box = decode_record(rec, gx, gy, anchors[ai], stride, width, height, width, height)

                        boxes.append({
                            "cls": cls,
                            "score": score,
                            "obj": obj,
                            "cls_score": cls_score,
                            "box": box,
                            "scale": scale_i,
                            "gy": gy,
                            "gx": gx,
                            "anchor": ai,
                        })

    return boxes


def run_sim(sim, inp):
    sim.set_input_tensor(0, nc.RuntimeTensor.from_numpy(inp))
    sim.run()

    outputs = []
    for i in range(3):
        outputs.append(sim.get_output_tensor(i).to_numpy())
    return outputs


def find_images(images_dir):
    exts = (".jpg", ".jpeg", ".png", ".bmp")
    files = []
    for fn in sorted(os.listdir(images_dir)):
        if fn.lower().endswith(exts):
            files.append(fn)
    return files


def draw_debug(image, gts, boxes, out_path):
    img = image.copy()

    # Draw GT green.
    for gt in gts:
        cv2.rectangle(img, (int(gt["x1"]), int(gt["y1"])), (int(gt["x2"]), int(gt["y2"])), (0, 255, 0), 2)
        name = LABELS[gt["cls"]] if 0 <= gt["cls"] < len(LABELS) else f"c{gt['cls']}"
        cv2.putText(img, f"GT {name}", (int(gt["x1"]), max(20, int(gt["y1"]) - 5)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 255, 0), 2)

    # Draw top score boxes red/blue.
    top = sorted(boxes, key=lambda b: -b["score"])[:10]
    for i, b in enumerate(top):
        x1, y1, x2, y2 = b["box"]
        color = (0, 0, 255) if b["cls"] == 1 else (255, 0, 0)
        name = LABELS[b["cls"]]
        cv2.rectangle(img, (int(x1), int(y1)), (int(x2), int(y2)), color, 1)
        cv2.putText(img, f"{name} {b['score']:.2f}", (int(x1), max(20, int(y1) - 5)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.45, color, 1)

    cv2.imwrite(out_path, img)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--kmodel", required=True)
    p.add_argument("--images-dir", required=True)
    p.add_argument("--labels-dir", required=True)
    p.add_argument("--width", type=int, default=640)
    p.add_argument("--height", type=int, default=480)
    p.add_argument("--limit", type=int, default=50)
    p.add_argument("--out-csv", default="final_kmodel_eval.csv")
    p.add_argument("--debug-dir", help="Optional folder to save debug images for first N images")
    p.add_argument("--debug-count", type=int, default=10)
    args = p.parse_args()

    print("=== final .kmodel YOLO-label evaluation ===")
    print("kmodel:", args.kmodel)
    print("images:", args.images_dir)
    print("labels:", args.labels_dir)
    print("limit :", args.limit)

    if args.debug_dir:
        os.makedirs(args.debug_dir, exist_ok=True)

    sim = nc.Simulator()
    sim.load_model(open(args.kmodel, "rb").read())

    image_files = find_images(args.images_dir)
    if args.limit > 0:
        image_files = image_files[:args.limit]

    rows = []
    total_gt = 0
    hit_by_th = {th: 0 for th in THRESHOLDS}

    for idx, fn in enumerate(image_files):
        image_path = os.path.join(args.images_dir, fn)
        stem = os.path.splitext(fn)[0]
        label_path = os.path.join(args.labels_dir, stem + ".txt")

        gts = read_yolo_label(label_path, args.width, args.height)
        if not gts:
            continue

        bgr, inp = image_to_tensor(image_path, args.width, args.height)
        outputs = run_sim(sim, inp)
        boxes = decode_outputs(outputs, args.width, args.height)

        if args.debug_dir and idx < args.debug_count:
            draw_debug(bgr, gts, boxes, os.path.join(args.debug_dir, stem + "_debug.jpg"))

        for gt_i, gt in enumerate(gts):
            total_gt += 1
            gt_box = [gt["x1"], gt["y1"], gt["x2"], gt["y2"]]

            same_cls_boxes = [b for b in boxes if b["cls"] == gt["cls"]]

            best_iou_box = max(same_cls_boxes, key=lambda b: iou_xyxy(b["box"], gt_box))
            best_score_box = max(same_cls_boxes, key=lambda b: b["score"])

            best_iou = iou_xyxy(best_iou_box["box"], gt_box)
            best_iou_score = best_iou_box["score"]

            best_score = best_score_box["score"]
            best_score_iou = iou_xyxy(best_score_box["box"], gt_box)

            for th in THRESHOLDS:
                detected = any((b["score"] >= th and iou_xyxy(b["box"], gt_box) >= 0.5) for b in same_cls_boxes)
                if detected:
                    hit_by_th[th] += 1

            rows.append({
                "image": fn,
                "gt_index": gt_i,
                "gt_cls": LABELS[gt["cls"]],
                "gt_x1": gt["x1"],
                "gt_y1": gt["y1"],
                "gt_x2": gt["x2"],
                "gt_y2": gt["y2"],
                "best_iou": best_iou,
                "best_iou_score": best_iou_score,
                "best_iou_obj": best_iou_box["obj"],
                "best_iou_cls_score": best_iou_box["cls_score"],
                "best_iou_scale": best_iou_box["scale"],
                "best_iou_cell": f"{best_iou_box['gy']},{best_iou_box['gx']}",
                "best_iou_anchor": best_iou_box["anchor"],
                "best_score": best_score,
                "best_score_iou": best_score_iou,
                "best_score_box": best_score_box["box"],
            })

        print(f"[{idx+1}/{len(image_files)}] {fn}: GT={len(gts)}")

    with open(args.out_csv, "w", newline="") as f:
        fieldnames = [
            "image", "gt_index", "gt_cls",
            "gt_x1", "gt_y1", "gt_x2", "gt_y2",
            "best_iou", "best_iou_score", "best_iou_obj", "best_iou_cls_score",
            "best_iou_scale", "best_iou_cell", "best_iou_anchor",
            "best_score", "best_score_iou", "best_score_box",
        ]
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    print("")
    print("=== summary ===")
    print("total GT:", total_gt)
    for th in THRESHOLDS:
        recall = hit_by_th[th] / total_gt if total_gt else 0
        print(f"recall@IoU0.5 score>={th}: {hit_by_th[th]}/{total_gt} = {recall:.3f}")

    if rows:
        avg_best_iou = sum(r["best_iou"] for r in rows) / len(rows)
        avg_best_iou_score = sum(r["best_iou_score"] for r in rows) / len(rows)
        avg_best_score_iou = sum(r["best_score_iou"] for r in rows) / len(rows)
        print("avg best_iou:", avg_best_iou)
        print("avg score of best-IoU candidate:", avg_best_iou_score)
        print("avg IoU of best-score candidate:", avg_best_score_iou)

    print("saved csv:", args.out_csv)
    if args.debug_dir:
        print("saved debug images:", args.debug_dir)


if __name__ == "__main__":
    main()

# pc_compare_kmodel_to_yolo_label.py
# Compare kmodel raw output locations to the actual YOLO label for one image.
#
# This does NOT depend on aicube postprocess.
# It answers:
#   "Is the model's strongest response near the ground-truth object?"
#
# Example:
# python3 pc_compare_kmodel_to_yolo_label.py \
#   --kmodel /workspace/cubetest/best_AnchorBaseDet_can3_5_n_20260514232500.kmodel \
#   --image /workspace/cubetest/1s0b_0010.jpg \
#   --label /datasets/my_dataset/val/labels/1s0b_0010.txt \
#   --out /workspace/cubetest/debug_compare.jpg

import argparse
import os
import cv2
import numpy as np
import nncase as nc


LABEL_NAMES = ["black", "silver"]
STRIDES = [8, 16, 32]


def load_image_tensor(image_path, width=640, height=480):
    bgr = cv2.imread(image_path, cv2.IMREAD_COLOR)
    if bgr is None:
        raise FileNotFoundError(image_path)

    bgr_resized = cv2.resize(bgr, (width, height), interpolation=cv2.INTER_LINEAR)
    rgb = cv2.cvtColor(bgr_resized, cv2.COLOR_BGR2RGB)
    inp = rgb.transpose(2, 0, 1)[None].astype(np.uint8)
    return bgr_resized, inp


def read_yolo_labels(label_path, img_w, img_h):
    boxes = []
    if not label_path or not os.path.exists(label_path):
        return boxes

    with open(label_path, "r") as f:
        for line in f:
            parts = line.strip().split()
            if len(parts) < 5:
                continue

            cls = int(float(parts[0]))
            xc = float(parts[1]) * img_w
            yc = float(parts[2]) * img_h
            bw = float(parts[3]) * img_w
            bh = float(parts[4]) * img_h

            x1 = xc - bw / 2
            y1 = yc - bh / 2
            x2 = xc + bw / 2
            y2 = yc + bh / 2

            boxes.append((cls, xc, yc, bw, bh, x1, y1, x2, y2))
    return boxes


def run_kmodel(kmodel_path, inp, try_outputs=3):
    sim = nc.Simulator()
    sim.load_model(open(kmodel_path, "rb").read())
    sim.set_input_tensor(0, nc.RuntimeTensor.from_numpy(inp))
    sim.run()

    outputs = []
    for i in range(try_outputs):
        try:
            out = sim.get_output_tensor(i).to_numpy()
            outputs.append(out)
        except Exception as e:
            print(f"output[{i}] read failed: {repr(e)}")
    return outputs


def analyze_output(out, stride, topk=20):
    # Expected out: (1,H,W,21)
    h = out.shape[1]
    w = out.shape[2]
    y = out[0].reshape(h, w, 3, 7)

    obj = y[..., 4]
    cls0 = y[..., 5]
    cls1 = y[..., 6]
    cls_max = np.maximum(cls0, cls1)
    score = obj * cls_max

    flat_idx = np.argsort(score.reshape(-1))[-topk:][::-1]

    rows = []
    for rank, fi in enumerate(flat_idx, 1):
        yy, xx, a = np.unravel_index(fi, score.shape)
        winner = 0 if cls0[yy, xx, a] >= cls1[yy, xx, a] else 1
        px = (xx + 0.5) * stride
        py = (yy + 0.5) * stride

        rows.append({
            "rank": rank,
            "stride": stride,
            "cell_y": yy,
            "cell_x": xx,
            "anchor": a,
            "px": px,
            "py": py,
            "obj": float(obj[yy, xx, a]),
            "cls0": float(cls0[yy, xx, a]),
            "cls1": float(cls1[yy, xx, a]),
            "score": float(score[yy, xx, a]),
            "winner": winner,
        })

    return rows


def draw_debug(bgr, gt_boxes, all_rows, out_path):
    img = bgr.copy()

    # Draw GT boxes in green-ish
    for box in gt_boxes:
        cls, xc, yc, bw, bh, x1, y1, x2, y2 = box
        name = LABEL_NAMES[cls] if 0 <= cls < len(LABEL_NAMES) else f"c{cls}"
        cv2.rectangle(img, (int(x1), int(y1)), (int(x2), int(y2)), (0, 255, 0), 2)
        cv2.putText(img, f"GT {name}", (int(x1), max(20, int(y1) - 8)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2)

    # Draw top raw-score cell centers.
    # Red: stride8, Blue: stride16, Yellow: stride32
    color_by_stride = {
        8: (0, 0, 255),
        16: (255, 0, 0),
        32: (0, 255, 255),
    }

    for row in all_rows:
        rank = row["rank"]
        stride = row["stride"]

        # Draw only top 10 per scale to avoid clutter.
        if rank > 10:
            continue

        x = int(row["px"])
        y = int(row["py"])
        color = color_by_stride.get(stride, (255, 255, 255))
        cv2.circle(img, (x, y), 5, color, -1)
        cv2.putText(img, f"s{stride}#{rank}", (x + 5, y - 5),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.45, color, 1)

    cv2.imwrite(out_path, img)
    print("Saved debug image:", out_path)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--kmodel", required=True)
    p.add_argument("--image", required=True)
    p.add_argument("--label", help="YOLO txt label path")
    p.add_argument("--out", default="debug_compare.jpg")
    p.add_argument("--width", type=int, default=640)
    p.add_argument("--height", type=int, default=480)
    p.add_argument("--topk", type=int, default=20)
    args = p.parse_args()

    print("=== Compare kmodel raw response to YOLO label ===")
    print("kmodel:", args.kmodel)
    print("image :", args.image)
    print("label :", args.label)

    bgr, inp = load_image_tensor(args.image, args.width, args.height)
    print("input shape:", inp.shape, inp.dtype, inp.min(), inp.max(), inp.mean())
    print("input first 20:", inp.reshape(-1)[:20])

    gt_boxes = read_yolo_labels(args.label, args.width, args.height)
    if not gt_boxes:
        print("WARNING: no YOLO labels loaded. This will only show top raw cells.")
    else:
        print("Ground truth boxes:")
        for box in gt_boxes:
            cls, xc, yc, bw, bh, x1, y1, x2, y2 = box
            name = LABEL_NAMES[cls] if 0 <= cls < len(LABEL_NAMES) else f"c{cls}"
            print(f"  {name}: center=({xc:.1f},{yc:.1f}) size=({bw:.1f},{bh:.1f}) box=({x1:.1f},{y1:.1f})-({x2:.1f},{y2:.1f})")

            for stride in STRIDES:
                print(f"    expected grid at stride {stride}: cell≈({int(yc // stride)},{int(xc // stride)})")

    outputs = run_kmodel(args.kmodel, inp, 3)

    all_rows = []
    for i, out in enumerate(outputs):
        if len(out.shape) != 4 or out.shape[-1] != 21:
            print(f"output[{i}] skipped shape={out.shape}")
            continue

        stride = STRIDES[i]
        print(f"\noutput[{i}] stride={stride} shape={out.shape} min={out.min():.6f} max={out.max():.6f} mean={out.mean():.6f}")

        rows = analyze_output(out, stride, args.topk)
        all_rows.extend(rows)

        print(f"Top {args.topk} raw cells for stride {stride}:")
        for r in rows[:args.topk]:
            winner = LABEL_NAMES[r["winner"]]
            print(
                f"  #{r['rank']:02d} pixel≈({r['px']:.1f},{r['py']:.1f}) "
                f"cell=({r['cell_y']},{r['cell_x']}) anchor={r['anchor']} "
                f"obj={r['obj']:.6f} cls0={r['cls0']:.6f} cls1={r['cls1']:.6f} "
                f"score={r['score']:.6f} -> {winner}"
            )

    draw_debug(bgr, gt_boxes, all_rows, args.out)


if __name__ == "__main__":
    main()

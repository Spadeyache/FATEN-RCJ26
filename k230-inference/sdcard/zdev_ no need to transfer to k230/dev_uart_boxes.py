# dev_uart_boxes.py -- K230 UART test frames for Teensy + box_viewer.html.
#
# Run this on the K230 instead of main.py when you want to test only the UART
# path. It sends fake YOLO boxes using the same wire protocol as robot_io.py.
# Teensy should print lines like:
#
#   K230D BOXES n=2
#   K230D BOX cls=0 score=204 x1=150 y1=200 x2=300 y2=350
#
# Then open box_viewer.html in Chrome/Edge and connect to the Teensy's USB
# serial port to see the boxes drawn.

import sys
import time

for _p in ("/sdcard/app", "/sdcard/customLib"):
    if _p not in sys.path:
        sys.path.insert(0, _p)

import robot_io


_seed = 0x12345678


def _rand(max_value):
    """Small deterministic PRNG so this works even if random is unavailable."""
    global _seed
    _seed = (_seed * 1103515245 + 12345) & 0x7fffffff
    return _seed % max_value


def _make_box(cls_id):
    w = 40 + _rand(180)
    h = 35 + _rand(150)
    x1 = _rand(640 - w)
    y1 = _rand(480 - h)
    x2 = x1 + w
    y2 = y1 + h
    score = 0.45 + (_rand(50) / 100.0)
    return [cls_id, score, x1, y1, x2, y2]


def main():
    print("=== dev_uart_boxes.py ===")
    print("Sending fake K230D boxes over robot_io UART.")
    u = robot_io.open_link()
    frame = 0

    try:
        while True:
            n = 1 + _rand(4)
            boxes = []
            for i in range(n):
                boxes.append(_make_box(i & 1))

            robot_io.send_boxes(u, boxes)
            print("sent frame={} boxes={}".format(frame, len(boxes)))

            frame += 1
            time.sleep_ms(250)
    except KeyboardInterrupt:
        print("\nstopping...")
    finally:
        try:
            u.deinit()
        except Exception:
            pass
        print("done.")


if __name__ == "__main__":
    main()

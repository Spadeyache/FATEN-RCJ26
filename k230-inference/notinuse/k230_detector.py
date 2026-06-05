# k230_detector.py — K230D object detection + Teensy UART communication
# Run standalone on K230D (CanMV MicroPython environment).
#
# Wiring:
#   K230D UART1 TX  →  Teensy RX5  (pin 21)
#   K230D UART1 RX  ←  Teensy TX5  (pin 20)
#   GND shared
#
# Protocol:
#   Teensy→K230D  1 byte every ~100 ms:
#     0x00 = IDLE   — stay at rest, do not run inference
#     0x01 = DETECT — run detection and send results
#
#   K230D→Teensy  after each inference (when DETECT):
#     [0xAA] [COUNT] ([TYPE][X][Y]) × COUNT [CHKSUM]
#       COUNT : 0–8, number of detected objects this frame
#       TYPE  : see object-type constants below
#       X, Y  : 0–255 normalised position (0=left/top, 255=right/bottom)
#       CHKSUM: XOR of every byte after the 0xAA header (COUNT + all TYPE/X/Y)
#
# Plug in your own AI model inside run_detection(). The rest handles comms.

from machine import UART
import time

# ---------------------------------------------------------------------------
#  Object type constants  (must match K230ObjectType enum in globals.h)
# ---------------------------------------------------------------------------
TYPE_ALIVE      = 1   # Live victim
TYPE_DEAD       = 2   # Dead victim (no movement)
TYPE_EVAC_RED   = 3   # Red evacuation point
TYPE_EVAC_GREEN = 4   # Green evacuation point
TYPE_OBSTACLE   = 5   # Generic obstacle

# ---------------------------------------------------------------------------
#  UART initialisation
# ---------------------------------------------------------------------------
uart = UART(UART.UART1, baudrate=115200,
            bits=UART.EIGHTBITS, parity=UART.PARITY_NONE, stop=UART.STOPBITS_ONE)

_running = False   # Latest command from Teensy

# ---------------------------------------------------------------------------
#  Frame builder
# ---------------------------------------------------------------------------
def build_frame(detections):
    """
    detections : list of (type, x, y) tuples, at most 8 entries.
                 x, y must be 0-255 (normalise your model output before calling).
    Returns    : bytes object ready to write to UART.
    """
    count = min(len(detections), 8)
    frame = bytearray()
    frame.append(0xAA)       # header (not included in checksum)
    frame.append(count)
    chk = count              # start XOR accumulator with COUNT
    for i in range(count):
        t = int(detections[i][0]) & 0xFF
        x = int(detections[i][1]) & 0xFF
        y = int(detections[i][2]) & 0xFF
        frame.append(t)
        frame.append(x)
        frame.append(y)
        chk ^= t ^ x ^ y
    frame.append(chk)
    return bytes(frame)

# ---------------------------------------------------------------------------
#  Command receiver
# ---------------------------------------------------------------------------
def check_command():
    """Drain UART RX; update _running from the latest byte received."""
    global _running
    latest = None
    while uart.any():
        b = uart.read(1)
        if b:
            latest = b[0]
    if latest is not None:
        _running = (latest == 0x01)

# ---------------------------------------------------------------------------
#  Detection stub — replace with your actual model inference
# ---------------------------------------------------------------------------
def run_detection():
    """
    Run object detection on one camera frame.

    Returns a list of (type, x, y) tuples (0–8 items).
    x and y must be normalised to 0-255 from your camera resolution.

    Example normalisation for a 320×240 frame:
        x_norm = int(bbox_cx / 320 * 255)
        y_norm = int(bbox_cy / 240 * 255)

    Replace the placeholder below with real KPU / sensor_ov5647 / OpenMV code.
    """
    # --- INSERT YOUR MODEL INFERENCE HERE ---
    # Example (static stub for wiring test):
    # return [
    #     (TYPE_ALIVE,      128, 100),
    #     (TYPE_EVAC_GREEN, 200, 180),
    # ]
    return []

# ---------------------------------------------------------------------------
#  Main loop
# ---------------------------------------------------------------------------
print("K230D detector ready — waiting for Teensy command")

while True:
    check_command()

    if _running:
        dets  = run_detection()
        frame = build_frame(dets)
        uart.write(frame)
        # Optional debug — remove if CanMV print is slow
        # print("sent:", len(dets), "detections")

    time.sleep_ms(50)   # ~20 Hz detection rate; lower if model is slower

# K230D Zero / CanMV v1.5-legacy
#
# TeensyComm.py -- Teensy <-> K230D UART link. Sits next to camera_vision.py
# in /data/scr/.
#
# K230 -> Teensy: variable-length box list
#
#   [0xAA] [0x55] [N] [box]xN [xor_checksum]
#
# where each box is 10 bytes:
#
#   [cls] [score] [x1_hi] [x1_lo] [y1_hi] [y1_lo] [x2_hi] [x2_lo] [y2_hi] [y2_lo]
#       cls    : 0=silver, 1=black (model output class id, NO swap done here)
#       score  : 0..255, = round(model_confidence * 255)
#       x1..y2 : signed 16-bit BE pixel coords in the SENSOR frame (post un-letterbox)
#
# checksum = XOR of every byte from 0xAA through the last box byte
#
# Teensy -> K230: single byte
#       0x00 = idle, 0x01 = run
#
# Public surface used by main2.py:
#
#   open_link()                              -> uart handle
#   read_command(u, current_state) -> bool   poll RX, return run/idle flag
#   send_boxes(u, boxes)                     send a frame's worth of boxes
#   send_empty(u)                            send a zero-count frame (heartbeat)

from machine import UART, FPIOA


# Tunables
UART_DEVICE      = UART.UART1
UART_BAUD        = 115200
UART_TX_PIN      = 11
UART_RX_PIN      = 12

SYNC0            = 0xAA
SYNC1            = 0x55
MAX_BOXES        = 16          # frame cap; protect against runaway counts

CMD_IDLE         = 0x00
CMD_RUN          = 0x01


# ----------------------------------------------------------------------------

def open_link():
    """Open UART + apply FPIOA pinmux. Returns the UART handle."""
    try:
        fpioa = FPIOA()
        fpioa.set_function(UART_TX_PIN, FPIOA.UART1_TXD)
        fpioa.set_function(UART_RX_PIN, FPIOA.UART1_RXD)
    except Exception as e:
        print("TeensyComm: FPIOA setup skipped:", e)
    u = UART(UART_DEVICE,
             baudrate=UART_BAUD,
             bits=UART.EIGHTBITS,
             parity=UART.PARITY_NONE,
             stop=UART.STOPBITS_ONE)
    print("TeensyComm: opened UART1 @{} baud, TX={} RX={}".format(
        UART_BAUD, UART_TX_PIN, UART_RX_PIN))
    return u


def read_command(u, current_state):
    """Drain pending bytes; return updated run-state.

    0x01 -> True (run), 0x00 -> False (idle). Other bytes ignored.
    Returns `current_state` unchanged if RX empty.
    """
    state = current_state
    n = u.any()
    if not n:
        return state
    try:
        data = u.read(n) or b""
    except Exception:
        return current_state
    for b in data:
        if b == CMD_RUN:
            state = True
        elif b == CMD_IDLE:
            state = False
    return state


def send_boxes(u, boxes):
    """Send a list of boxes.

    boxes : iterable of [cls, score_0_to_1, x1, y1, x2, y2] (the same shape
            main2.py's `det` list uses after _unletterbox_box).
    """
    n = len(boxes)
    if n > MAX_BOXES:
        n = MAX_BOXES

    buf = bytearray(3 + n * 10 + 1)
    buf[0] = SYNC0
    buf[1] = SYNC1
    buf[2] = n
    chk = SYNC0 ^ SYNC1 ^ n
    idx = 3

    for i in range(n):
        b = boxes[i]
        cls   = int(b[0]) & 0xFF
        score = int(float(b[1]) * 255)
        if score < 0:   score = 0
        if score > 255: score = 255
        x1 = _clamp16(b[2])
        y1 = _clamp16(b[3])
        x2 = _clamp16(b[4])
        y2 = _clamp16(b[5])

        buf[idx] = cls;            chk ^= cls;            idx += 1
        buf[idx] = score;          chk ^= score;          idx += 1
        buf[idx] = (x1 >> 8) & 0xFF; chk ^= buf[idx];     idx += 1
        buf[idx] = x1 & 0xFF;      chk ^= buf[idx];       idx += 1
        buf[idx] = (y1 >> 8) & 0xFF; chk ^= buf[idx];     idx += 1
        buf[idx] = y1 & 0xFF;      chk ^= buf[idx];       idx += 1
        buf[idx] = (x2 >> 8) & 0xFF; chk ^= buf[idx];     idx += 1
        buf[idx] = x2 & 0xFF;      chk ^= buf[idx];       idx += 1
        buf[idx] = (y2 >> 8) & 0xFF; chk ^= buf[idx];     idx += 1
        buf[idx] = y2 & 0xFF;      chk ^= buf[idx];       idx += 1

    buf[idx] = chk & 0xFF
    u.write(bytes(buf))


def send_empty(u):
    """Send a zero-count frame. Use as a heartbeat / 'still alive, no targets' tick."""
    chk = SYNC0 ^ SYNC1 ^ 0
    u.write(bytes([SYNC0, SYNC1, 0, chk & 0xFF]))


# ----------------------------------------------------------------------------

def _clamp16(v):
    v = int(v)
    if v >  32767: v =  32767
    if v < -32768: v = -32768
    return v & 0xFFFF

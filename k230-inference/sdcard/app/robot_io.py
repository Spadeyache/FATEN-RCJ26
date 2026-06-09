# app/robot_io.py -- Teensy <-> K230D UART link.
#
# Wire protocol (K230 -> Teensy, variable-length box list):
#
#   [0xAA] [0x55] [N] [box]xN [xor_checksum]
#
# where each box is 10 bytes:
#
#   [cls] [score] [x1_hi] [x1_lo] [y1_hi] [y1_lo] [x2_hi] [x2_lo] [y2_hi] [y2_lo]
#       cls    : 0=silver, 1=black (model output class id, NO swap done here)
#       score  : 0..255 = round(model_confidence * 255)
#       x1..y2 : signed 16-bit BE pixel coords in SENSOR frame
#
# checksum = XOR of every byte from 0xAA through the last box byte
#
# Teensy -> K230 (1 byte):
#   0x00 = idle, 0x01 = run
#
# Public API:
#   open_link()                          -> uart handle
#   read_command(u, current_state)       drain RX, return run/idle flag
#   send_boxes(u, boxes)                 send a frame's worth of boxes
#   send_empty(u)                        send a zero-count frame (heartbeat)

from machine import UART, FPIOA

import config


SYNC0    = 0xAA
SYNC1    = 0x55
CMD_IDLE = 0x00
CMD_RUN  = 0x01


def open_link():
    """Open UART2 + apply FPIOA pinmux. Returns the UART handle."""
    try:
        fpioa = FPIOA()
        fpioa.set_function(config.UART_TX_PIN, FPIOA.UART2_TXD)
        fpioa.set_function(config.UART_RX_PIN, FPIOA.UART2_RXD)
    except Exception as e:
        print("robot_io: FPIOA setup skipped:", e)
    u = UART(UART.UART2,
             baudrate=config.UART_BAUD,
             bits=UART.EIGHTBITS,
             parity=UART.PARITY_NONE,
             stop=UART.STOPBITS_ONE)
    print("robot_io: opened UART2 @{} baud, TX={} RX={}".format(
        config.UART_BAUD, config.UART_TX_PIN, config.UART_RX_PIN))
    return u


def read_command(u, current_state):
    """Drain pending bytes; return updated run-state.

    0x01 -> True (run), 0x00 -> False (idle). Other bytes ignored.
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

    boxes : iterable of [cls, score_0_to_1, x1, y1, x2, y2]
    """
    n = len(boxes)
    if n > config.MAX_BOXES_TX:
        n = config.MAX_BOXES_TX

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
    """Send a zero-count frame. Heartbeat / 'still alive, no targets'."""
    chk = SYNC0 ^ SYNC1 ^ 0
    u.write(bytes([SYNC0, SYNC1, 0, chk & 0xFF]))


def _clamp16(v):
    v = int(v)
    if v >  32767: v =  32767
    if v < -32768: v = -32768
    return v & 0xFFFF

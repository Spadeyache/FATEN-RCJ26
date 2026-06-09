# app/utils.py -- shared runtime helpers and Teensy UART protocol.

from machine import UART, FPIOA

import camera_profile as profile


SYNC0 = 0xAA
SYNC1 = 0x55
CMD_REST = 0x00
CMD_DETECTION = 0x01

# Compatibility aliases for older scripts.
CMD_IDLE = CMD_REST
CMD_RUN = CMD_DETECTION


def open_link():
    try:
        fpioa = FPIOA()
        fpioa.set_function(profile.UART_TX_PIN, FPIOA.UART1_TXD)
        fpioa.set_function(profile.UART_RX_PIN, FPIOA.UART1_RXD)
    except Exception as e:
        print("utils: FPIOA setup skipped:", e)
    u = UART(UART.UART1,
             baudrate=profile.UART_BAUD,
             bits=UART.EIGHTBITS,
             parity=UART.PARITY_NONE,
             stop=UART.STOPBITS_ONE)
    print("utils: opened UART1 @{} baud, TX={} RX={}".format(
        profile.UART_BAUD, profile.UART_TX_PIN, profile.UART_RX_PIN))
    return u


def read_command(u, current_state):
    state = current_state
    n = u.any()
    if not n:
        return state
    try:
        data = u.read(n) or b""
    except Exception:
        return current_state
    for b in data:
        if b == CMD_DETECTION:
            state = True
        elif b == CMD_REST:
            state = False
    return state


def send_boxes(u, boxes):
    n = len(boxes)
    if n > profile.MAX_BOXES_TX:
        n = profile.MAX_BOXES_TX

    buf = bytearray(3 + n * 10 + 1)
    buf[0] = SYNC0
    buf[1] = SYNC1
    buf[2] = n
    chk = SYNC0 ^ SYNC1 ^ n
    idx = 3

    for i in range(n):
        b = boxes[i]
        cls = int(b[0]) & 0xFF
        score = int(float(b[1]) * 255)
        if score < 0:
            score = 0
        if score > 255:
            score = 255
        x1 = _clamp16(b[2])
        y1 = _clamp16(b[3])
        x2 = _clamp16(b[4])
        y2 = _clamp16(b[5])

        buf[idx] = cls; chk ^= cls; idx += 1
        buf[idx] = score; chk ^= score; idx += 1
        buf[idx] = (x1 >> 8) & 0xFF; chk ^= buf[idx]; idx += 1
        buf[idx] = x1 & 0xFF; chk ^= buf[idx]; idx += 1
        buf[idx] = (y1 >> 8) & 0xFF; chk ^= buf[idx]; idx += 1
        buf[idx] = y1 & 0xFF; chk ^= buf[idx]; idx += 1
        buf[idx] = (x2 >> 8) & 0xFF; chk ^= buf[idx]; idx += 1
        buf[idx] = x2 & 0xFF; chk ^= buf[idx]; idx += 1
        buf[idx] = (y2 >> 8) & 0xFF; chk ^= buf[idx]; idx += 1
        buf[idx] = y2 & 0xFF; chk ^= buf[idx]; idx += 1

    buf[idx] = chk & 0xFF
    u.write(bytes(buf))


def send_empty(u):
    chk = SYNC0 ^ SYNC1 ^ 0
    u.write(bytes([SYNC0, SYNC1, 0, chk & 0xFF]))


def _clamp16(v):
    v = int(v)
    if v > 32767:
        v = 32767
    if v < -32768:
        v = -32768
    return v & 0xFFFF

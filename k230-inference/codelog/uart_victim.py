# K230D Zero / CanMV v1.5-legacy
#
# uart_victim.py -- the UART link to the Teensy for "single best victim"
# detection packets. Lives alongside camera_vision.py in /data/scr/.
#
# Wire protocol  K230 -> Teensy, 8 bytes fixed:
#
#   [0xAA] [0x55] [type] [score] [x_hi] [x_lo] [y_hi] [y_lo]
#
#     type  = 0xFF (none) | 0 (K230_BLACK) | 1 (K230_SILVER)
#     score = round(model_confidence * 255), 0..255
#     x, y  = pixel center in the SENSOR frame, 16-bit big-endian
#
# Optional  Teensy -> K230, single byte:
#
#     0x00 = idle  ->  caller can pause inference
#     0x01 = run   ->  caller can resume inference
#
# Public surface (used by main.py / any deploy script):
#
#   open_link()                          one-time UART + FPIOA setup; returns handle
#   send_victim(u, model_cls, score, x, y)   send the best detection
#   send_none(u)                         send the 'nothing here' packet
#   read_command(u, current_state)       drain RX, return updated run-state
#
# All tunables live at the top -- edit them here, nowhere else.

from machine import UART, FPIOA


# ============================================================================
# Tunables -- edit these.
# ============================================================================

UART_DEVICE      = UART.UART1
UART_BAUD        = 115200

# FPIOA pinmux for the K230D Zero. Adjust if your board wires UART1 elsewhere.
UART_TX_PIN      = 11
UART_RX_PIN      = 12

# Wire framing
SYNC0            = 0xAA
SYNC1            = 0x55
TYPE_NONE        = 0xFF

# Model class id -> wire class id.
# YOLOv8 was trained as data.yaml ["silver", "black"], so:
#     model cls 0  =  silver
#     model cls 1  =  black
# Teensy enum K230ObjectType (globals.h):
#     K230_BLACK  = 0
#     K230_SILVER = 1
# So the swap is { 0 -> 1, 1 -> 0 }.
MODEL_TO_WIRE    = [1, 0]


# ============================================================================
# Public API
# ============================================================================

def open_link():
    """Open UART + apply FPIOA pinmux. Returns the UART handle.

    Safe to call once at startup. If the FPIOA call fails (already routed),
    the error is logged and the UART is still constructed.
    """
    try:
        fpioa = FPIOA()
        fpioa.set_function(UART_TX_PIN, FPIOA.UART1_TXD)
        fpioa.set_function(UART_RX_PIN, FPIOA.UART1_RXD)
    except Exception as e:
        print("uart_victim: FPIOA setup skipped:", e)
    u = UART(UART_DEVICE,
             baudrate=UART_BAUD,
             bits=UART.EIGHTBITS,
             parity=UART.PARITY_NONE,
             stop=UART.STOPBITS_ONE)
    print("uart_victim: opened UART1 @{} baud, TX={} RX={}".format(
        UART_BAUD, UART_TX_PIN, UART_RX_PIN))
    return u


def send_victim(u, model_cls, score_0_to_1, x_px, y_px):
    """Send the single best detection. Handles model -> wire class translation.

    model_cls       int, what the kmodel emitted (0=silver, 1=black)
    score_0_to_1    float in [0, 1] -- gets clamped + scaled to uint8
    x_px, y_px      pixel center in SENSOR coords (after un-letterbox)
    """
    if 0 <= model_cls < len(MODEL_TO_WIRE):
        wire_cls = MODEL_TO_WIRE[model_cls]
    else:
        wire_cls = TYPE_NONE
    _send_raw(u, wire_cls, int(score_0_to_1 * 255), x_px, y_px)


def send_none(u):
    """Send the 'no detection' packet (type=0xFF, score=0, coords=0)."""
    _send_raw(u, TYPE_NONE, 0, 0, 0)


def read_command(u, current_state):
    """Drain pending bytes from the Teensy; return the updated run-state.

    Bytes interpreted:
        0x01 -> True  (run)
        0x00 -> False (idle)
    Anything else is ignored. Returns `current_state` unchanged if RX empty.
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
        if b == 0x01:
            state = True
        elif b == 0x00:
            state = False
    return state


# ============================================================================
# Internal
# ============================================================================

def _send_raw(u, type_id, score_u8, x_px, y_px):
    """Construct + write the 8-byte fixed packet."""
    x = max(0, min(65535, int(x_px)))
    y = max(0, min(65535, int(y_px)))
    s = max(0, min(255, int(score_u8)))
    t = type_id & 0xFF
    u.write(bytes([SYNC0, SYNC1, t, s,
                   (x >> 8) & 0xFF, x & 0xFF,
                   (y >> 8) & 0xFF, y & 0xFF]))

# app/status_led.py -- one-pixel K230 status indicator.

import config


_np = None
_last_status = None
_last_color = None
_failed = False


def init():
    global _np, _failed
    if not config.STATUS_LED_ENABLED:
        return
    try:
        from machine import Pin
        import neopixel
        _np = neopixel.NeoPixel(Pin(config.NEOPIXEL_PIN),
                                config.NEOPIXEL_PIXELS)
    except Exception as e:
        _failed = True
        print("status_led: init FAILED ({})".format(e))


def set_status(status, force=False):
    global _last_status, _last_color, _failed
    if _failed or _np is None:
        return
    if not force and status == _last_status:
        return

    if status == "found":
        color = config.STATUS_COLOR_FOUND
    elif status == "boot":
        color = config.STATUS_COLOR_BOOT
    elif status == "run":
        color = config.STATUS_COLOR_RUN
    elif status == "evac":
        color = config.STATUS_COLOR_EVAC
    else:
        color = config.STATUS_COLOR_REST

    _last_status = status
    _last_color = color
    _write(color)


def refresh():
    """Re-drive the last color. Call every loop iteration: this NeoPixel does
    not hold its value between writes, so without a periodic refresh it goes
    dark and only blips on at the moment set_status changes the status."""
    if _failed or _np is None or _last_color is None:
        return
    _write(_last_color)


def _write(color):
    global _failed
    try:
        for i in range(config.NEOPIXEL_PIXELS):
            _np[i] = color
        _np.write()
    except Exception as e:
        _failed = True
        print("status_led: write FAILED ({})".format(e))


def set_detecting(detecting, force=False):
    # Backward-compatible wrapper: True means evac/run, False means rest.
    set_status("evac" if detecting else "rest", force)

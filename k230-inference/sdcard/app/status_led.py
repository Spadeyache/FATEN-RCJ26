# app/status_led.py -- one-pixel K230 status indicator.

import config


_np = None
_last_status = None
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
    global _last_status, _failed
    if _failed or _np is None:
        return
    if not force and status == _last_status:
        return

    if status == "found":
        color = config.STATUS_COLOR_FOUND
    elif status == "evac":
        color = config.STATUS_COLOR_EVAC
    else:
        color = config.STATUS_COLOR_REST

    try:
        for i in range(config.NEOPIXEL_PIXELS):
            _np[i] = color
        _np.write()
        _last_status = status
    except Exception as e:
        _failed = True
        print("status_led: write FAILED ({})".format(e))


def set_detecting(detecting, force=False):
    # Backward-compatible wrapper: True means evac/run, False means rest.
    set_status("evac" if detecting else "rest", force)

# bootserial - By: yache - Sun May 3 2026
# The micropython image initialized when the power is connected and we want to bypass this.
# when we operate K230d via external power supply and want to connect to USB-OTG, we must initalize the pin.

import sys
import usb

u = usb.Serial("/dev/ttyUSB")
u.open()

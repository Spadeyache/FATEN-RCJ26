import time
from media.sensor import *
from media.display import *
from media.media import *

sensor = Sensor()
sensor.reset()
sensor.set_framesize(Sensor.VGA)
sensor.set_pixformat(Sensor.RGB565)

# Streams live video directly into CanMV IDE window
Display.init(Display.VIRT, sensor.width(), sensor.height(), to_ide=True)
MediaManager.init()
sensor.run()

try:
    while True:
        img = sensor.snapshot()
        Display.show_image(img)
        time.sleep_ms(20)

except KeyboardInterrupt:
    pass

finally:
    sensor.stop()
    Display.deinit()
    MediaManager.deinit()

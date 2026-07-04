# maincam_rgb.py -- same as cameraCapIMGv2.py but captures in RGB888 colour.
# Press KEY button (pin 0) to capture | CTRL+C to quit
import time, os
from media.sensor import *
from media.display import *
from media.media import *
from machine import Pin

# --- config --- change these ---
CLASS_NAME = "rgb"
SAVE_DIR = f"/data/dataset/{CLASS_NAME}"

# --- folder setup ---
def mkdir_p(path):
    parts = path.strip('/').split('/')
    current = ''
    for part in parts:
        current += '/' + part
        try:
            os.mkdir(current)
        except OSError:
            pass

mkdir_p(SAVE_DIR)
count = len(os.listdir(SAVE_DIR))

print(f"=============================")
print(f" Class    : {CLASS_NAME}")
print(f" Save dir : {SAVE_DIR}")
print(f" Existing : {count} images")
print(f" Mode     : RGB888 colour")
print(f" Press BOOT button to capture")
print(f"=============================")

# --- status LED (blue = running, green flash = captured) ---
try:
    import neopixel
    _np = neopixel.NeoPixel(Pin(35), 1)
    def led(color):
        _np[0] = color
        _np.write()
except Exception as e:
    print("LED disabled:", e)
    def led(color):
        pass

# --- button ---
btn = Pin(0, Pin.IN, Pin.PULL_UP)
last_press = 0

# --- camera ---
sensor = Sensor()
sensor.reset()
sensor.set_framesize(Sensor.VGA)
sensor.set_pixformat(Sensor.RGB888)
Display.init(Display.VIRT, sensor.width(), sensor.height(), to_ide=True)
MediaManager.init()
sensor.run()
led((0, 0, 255))

try:
    while True:
        img = sensor.snapshot()

        Display.show_image(img)

        now = time.ticks_ms()
        if btn.value() == 0 and time.ticks_diff(now, last_press) > 500:
            filename = f"{SAVE_DIR}/{CLASS_NAME}_{count:04d}.jpg"
            # img.save() can't handle RGB888 directly ("operation not
            # supported") -- convert to RGB565 first; the JPEG stays colour.
            try:
                img.save(filename, quality=90)
            except Exception:
                img.to_rgb565().save(filename, quality=90)
            count += 1
            last_press = now
            print(f"✅ [{count}] {filename}")
            led((0, 255, 0))
            time.sleep_ms(300)
            led((0, 0, 255))

        time.sleep_ms(20)

except KeyboardInterrupt:
    pass

finally:
    led((0, 0, 0))
    sensor.stop()
    Display.deinit()
    MediaManager.deinit()
    print(f"=============================")
    print(f" Done! {count} images saved")
    print(f" Location: {SAVE_DIR}")
    print(f"=============================")

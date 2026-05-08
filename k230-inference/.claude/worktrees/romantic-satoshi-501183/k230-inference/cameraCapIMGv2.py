# image_collector.py
# Press KEY button (pin 0) to capture | CTRL+C to quit
import time, os
from media.sensor import *
from media.display import *
from media.media import *
from machine import Pin

# --- config --- change these ---
CLASS_NAME = "2s1b"
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
print(f" Mode     : Grayscale + histeq")
print(f" Press BOOT button to capture")
print(f"=============================")

# --- button ---
btn = Pin(0, Pin.IN, Pin.PULL_UP)
last_press = 0

# --- camera ---
sensor = Sensor()
sensor.reset()
sensor.set_framesize(Sensor.VGA)      # 320x240 — faster, enough for YOLOv8n
sensor.set_pixformat(Sensor.GRAYSCALE)
Display.init(Display.VIRT, sensor.width(), sensor.height(), to_ide=True)
MediaManager.init()
sensor.run()

try:
    while True:
        img = sensor.snapshot()

        # built-in histogram equalization — no to_bytes() needed
        # works directly on the image object in-place
        img.histeq()

        Display.show_image(img)

        now = time.ticks_ms()
        if btn.value() == 0 and time.ticks_diff(now, last_press) > 500:
            filename = f"{SAVE_DIR}/{CLASS_NAME}_{count:04d}.jpg"
            img.save(filename, quality=90)
            count += 1
            last_press = now
            print(f"✅ [{count}] {filename}")

        time.sleep_ms(20)

except KeyboardInterrupt:
    pass

finally:
    sensor.stop()
    Display.deinit()
    MediaManager.deinit()
    print(f"=============================")
    print(f" Done! {count} images saved")
    print(f" Location: {SAVE_DIR}")
    print(f"=============================")

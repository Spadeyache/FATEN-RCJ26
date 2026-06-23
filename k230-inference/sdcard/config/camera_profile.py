# config/camera_profile.py -- camera, detector, UART, and debug tunables.
#
# /data/calibration/camera.json may override exposure_us, gain_db, hmirror,
# and vflip without changing this file.

# Camera
WIDTH = 640
HEIGHT = 480
CAMERA_PIXFORMAT = "GRAYSCALE"
HMIRROR = False
VFLIP = False

AEC_AUTO = False
EXPOSURE_US = 8000
AGC_AUTO = False
GAIN_DB = 0.0
AWB_AUTO = False
WHITEBAL_RGB_DB = (0.0, 0.0, 0.0)
BLC_AUTO = False

# Capture
CAPTURE_BUTTON = 0
BUTTON_DEBOUNCE_MS = 400
JPEG_QUALITY = 95

# Detection
NUM_ANCHORS_640x480 = 6300
CONF_THRESHOLD = 0.30
NMS_THRESHOLD = 0.50
MAX_BOXES_TX = 16

DEFAULT_CATEGORIES = ["silver", "black"]
DEFAULT_IMG_SIZE = [640, 480]
DEFAULT_MODEL_TYPE = "AnchorFreeDet"

# Robot I/O (Teensy UART)
UART_BAUD = 115200
UART_TX_PIN = 11
UART_RX_PIN = 12

# Startup mode
BOOT_IN_DETECTION_MODE = True

# Display / debug
SHOW_DISPLAY = True
DEBUG_EVERY = 30
SETTLE_MS = 200
COLOR_PALETTE = [(220, 20, 60), (119, 11, 32),
                 (0, 0, 142), (0, 0, 230)]

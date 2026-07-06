# config.py -- all tunables for the K230D app. Single source of truth.
#
# The filesystem layout this assumes:
#
#   /sdcard/main.py
#   /sdcard/boot.py
#   /sdcard/app/{camera,detector,robot_io,config}.py
#   /sdcard/customLib/yolov8_decode.py
#
#   /data/models/<model>.kmodel
#   /data/models/deploy_config.json     (kmodel_path = "<model>.kmodel")
#   /data/models/labels.txt             (optional; one label per line)
#   /data/calibration/camera.json       (optional camera overrides)
#   /data/images/                       (button-press captures)
#   /data/logs/                         (reserved)

# ============================================================================
# Paths
# ============================================================================
MODELS_DIR         = "/data/models"
DEPLOY_CONFIG_PATH = MODELS_DIR + "/colordet_deploy_config.json"  # colour model (Black/Blue/Green/Orange/Red/Silver/Yellow)
POINTS_DEPLOY_CONFIG = MODELS_DIR + "/points_deploy_config.json"  # points model (corner colour)
LABELS_PATH        = MODELS_DIR + "/colordet_labels.txt"
KMODEL_DEFAULT     = MODELS_DIR + "/colordet.kmodel"   # used only if deploy_config absent

CAPTURE_DIR        = "/data/captures"
LOG_DIR            = "/data/logs"
CAMERA_CALIB_PATH  = "/data/calibration/camera.json"


# ============================================================================
# Camera (defaults; /data/calibration/camera.json overrides per-field)
# ============================================================================
WIDTH              = 640
HEIGHT             = 480
CAMERA_PIXFORMAT   = "RGB888"  # colordet model trained on RGB888 captures (maincam_rgb.py) — keep in sync
HMIRROR            = False
VFLIP              = False

AEC_AUTO           = False     # False = locked at EXPOSURE_US
EXPOSURE_US        = 8000
AGC_AUTO           = False     # False = locked at GAIN_DB
GAIN_DB            = 0.0
AWB_AUTO           = False
WHITEBAL_RGB_DB    = (0.0, 0.0, 0.0)
BLC_AUTO           = False     # NOTE: stubbed on gc2093_csi2 -- no-op for now

# Capture (BOOT button -> SD card)
CAPTURE_BUTTON     = 0
BUTTON_DEBOUNCE_MS = 400
JPEG_QUALITY       = 95


# ============================================================================
# Detection
# ============================================================================
NUM_ANCHORS_640x480 = 6300        # 80*60 + 40*30 + 20*15
CONF_THRESHOLD      = 0.30
NMS_THRESHOLD       = 0.50
MAX_BOXES_TX        = 16          # hard cap per wire frame (must match robot_io.MAX_BOXES)
TX_TOP_N            = 3           # only the N highest-confidence boxes are sent to the Teensy
                                 # (the K230 display still draws ALL boxes above CONF_THRESHOLD)
BYPASS_AI2D_DIRECT  = False       # Experimental. True can be faster, but may
                                 # trigger ndarray malloc fail on K230 heap.
MAX_BOXES_DRAW      = 12          # draw at most this many boxes; avoids slow/crashy preview bursts


# ============================================================================
# Robot I/O (Teensy UART2)
# ============================================================================
UART_BAUD          = 115200
UART_TX_PIN        = 11
UART_RX_PIN        = 12


# ============================================================================
# Status NeoPixel
# ============================================================================
STATUS_LED_ENABLED = True
NEOPIXEL_PIN       = 35
NEOPIXEL_PIXELS    = 1
STATUS_COLOR_BOOT  = (255, 255, 255) # white: booting / loading kmodel
STATUS_COLOR_REST  = (255, 255, 255) # white: rest / idle
STATUS_COLOR_RUN   = (0, 255, 255)   # cyan: running, no detection
STATUS_COLOR_EVAC  = (0, 0, 255)     # blue: evac mode, no victim
STATUS_COLOR_FOUND = (0, 255, 0)     # green: at least one victim


# ============================================================================
# Display / debug
# ============================================================================
SHOW_DISPLAY       = True
DISPLAY_EVERY_N    = 1     # Show preview every N frames. Raise to 2/3/5 if display costs too much.
DRAW_BOXES         = True  # Set False to keep preview but skip rectangle drawing.
DRAW_LABELS        = False  # class name+score text on boxes. Off by default:
                           # this firmware prints "Deprecated function..." on
                           # EVERY draw_string call, spamming the log each
                           # frame. Boxes stay colour-coded per class (see
                           # COLOR_PALETTE), which is enough to identify them.
DEBUG_EVERY        = 30
PROFILE_TIMING     = True
SETTLE_MS          = 200
# One draw colour per colordet class id (0=Black .. 6=Yellow). Chosen to be
# visible on the preview, echoing the class name where a literal colour would
# be invisible (Black -> light grey).
COLOR_PALETTE      = [(200, 200, 200),  # 0 Black
                      (0,     0, 255),  # 1 Blue
                      (0,   255,   0),  # 2 Green
                      (255, 140,   0),  # 3 Orange
                      (255,   0,   0),  # 4 Red
                      (192, 192, 192),  # 5 Silver
                      (255, 255,   0)]  # 6 Yellow

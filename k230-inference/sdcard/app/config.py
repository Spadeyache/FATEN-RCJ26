# config.py -- all tunables for the K230D app. Single source of truth.
#
# The filesystem layout this assumes:
#
#   /sdcard/main.py
#   /sdcard/boot.py
#   /sdcard/app/{camera,detector,robot_io,config}.py
#   /sdcard/customLib/yolov8_decode.py
#
#   /data/models/victim.kmodel
#   /data/models/deploy_config.json     (kmodel_path = "victim.kmodel")
#   /data/calibration/camera.json       (optional camera overrides)
#   /data/images/                       (button-press captures)
#   /data/logs/                         (reserved)

# ============================================================================
# Paths
# ============================================================================
MODELS_DIR         = "/data/models"
DEPLOY_CONFIG_PATH = MODELS_DIR + "/deploy_config.json"
KMODEL_DEFAULT     = MODELS_DIR + "/victim.kmodel"   # used only if deploy_config absent

CAPTURE_DIR        = "/data/captures"
LOG_DIR            = "/data/logs"
CAMERA_CALIB_PATH  = "/data/calibration/camera.json"


# ============================================================================
# Camera (defaults; /data/calibration/camera.json overrides per-field)
# ============================================================================
WIDTH              = 640
HEIGHT             = 480
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
MAX_BOXES_TX        = 16          # cap per wire frame (must match robot_io.MAX_BOXES)


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
STATUS_COLOR_REST  = (255, 255, 255) # white: rest / idle
STATUS_COLOR_EVAC  = (0, 0, 255)     # blue: evac mode, no victim
STATUS_COLOR_FOUND = (0, 255, 0)     # green: at least one victim


# ============================================================================
# Display / debug
# ============================================================================
SHOW_DISPLAY       = True
DEBUG_EVERY        = 30
SETTLE_MS          = 200
COLOR_PALETTE      = [(220, 20, 60), (119, 11, 32),
                      (0,   0, 142), (0,   0, 230)]

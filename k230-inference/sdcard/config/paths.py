# config/paths.py -- absolute K230D filesystem paths.
#
# Copy the repository's sdcard/ directory to /sdcard and data/ to /data on
# the module. Runtime code should use these constants instead of local PC paths.

SDCARD_ROOT = "/sdcard"
DATA_ROOT = "/data"

APP_DIR = SDCARD_ROOT + "/app"
CONFIG_DIR = SDCARD_ROOT + "/config"
CUSTOM_LIB_DIR = SDCARD_ROOT + "/customLib"

MODELS_DIR = DATA_ROOT + "/models"
KMODEL_PATH = MODELS_DIR + "/victim.kmodel"
DEPLOY_CONFIG_PATH = MODELS_DIR + "/deploy_config.json"

# Some CanMV file browsers expose "data" under the SD card root even when the
# intended on-chip layout is /data. The detector probes these as fallbacks.
KMODEL_FALLBACK_PATHS = [
    "/sdcard/data/models/victim.kmodel",
    "/sdcard/models/victim.kmodel",
]

LOG_DIR = DATA_ROOT + "/logs"
CAPTURE_DIR = DATA_ROOT + "/captures"
CALIBRATION_DIR = DATA_ROOT + "/calibration"
CAMERA_CALIB_PATH = CALIBRATION_DIR + "/camera.json"

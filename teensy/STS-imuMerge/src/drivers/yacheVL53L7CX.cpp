#include "yacheVL53L7CX.h"

yacheVL53L7CX::yacheVL53L7CX(TwoWire& bus, int lpn_pin, int i2c_rst_pin)
    : _drv(&bus, lpn_pin, i2c_rst_pin),
      _res(TOF_RES),
      _dx(0.0f), _dy(0.0f), _yaw(0.0f),
      _ready(false)
{}

bool yacheVL53L7CX::begin(uint8_t res_per_side, uint8_t freq_hz, uint8_t i2c_addr) {
    _res = (res_per_side == 8) ? 8 : 4;

    if (_drv.begin() != 0)                                    return false;
    if (_drv.init_sensor(i2c_addr) != 0)                      return false;

    uint8_t res_macro = (_res == 8) ? VL53L7CX_RESOLUTION_8X8 : VL53L7CX_RESOLUTION_4X4;
    if (_drv.vl53l7cx_set_resolution(res_macro) != 0)         return false;
    if (_drv.vl53l7cx_set_ranging_frequency_hz(freq_hz) != 0) return false;
    if (_drv.vl53l7cx_start_ranging() != 0)                   return false;

    _ready = true;
    return true;
}

bool yacheVL53L7CX::dataReady() {
    if (!_ready) return false;
    uint8_t r = 0;
    if (_drv.vl53l7cx_check_data_ready(&r) != 0) return false;
    return r != 0;
}

bool yacheVL53L7CX::getRanges(int16_t* mm_out, uint8_t* status_out) {
    if (!_ready || !mm_out || !status_out) return false;

    VL53L7CX_ResultsData r;
    if (_drv.vl53l7cx_get_ranging_data(&r) != 0) return false;

    const uint16_t n = zoneCount();
    for (uint16_t i = 0; i < n; ++i) {
        mm_out[i]     = r.distance_mm  [i * VL53L7CX_NB_TARGET_PER_ZONE];
        status_out[i] = r.target_status[i * VL53L7CX_NB_TARGET_PER_ZONE];
    }
    return true;
}

void yacheVL53L7CX::setMountTransform(float dx_mm, float dy_mm, float yaw_rad) {
    _dx  = dx_mm;
    _dy  = dy_mm;
    _yaw = yaw_rad;
}

float yacheVL53L7CX::zoneBearing(uint16_t idx) const {
    // Row affects elevation (ignored for ground-plane mapping in phase 1).
    const uint8_t col = (uint8_t)(idx % _res);
    const float fov_rad   = (TOF_FOV_DEG * (float)PI) / 180.0f;
    const float step_rad  = fov_rad / (float)_res;
    return (fov_rad * 0.5f) - ((float)col + 0.5f) * step_rad;
}

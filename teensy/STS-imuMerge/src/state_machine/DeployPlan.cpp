#include "DeployPlan.h"
#include "../../config.h"
#include "../sensors/XIAO_link.h"

namespace DeployPlan {

namespace {
    uint8_t _leftDeploy = GREEN_RIGHT_TURN_COUNT_LEFT;
    uint8_t _rightDeploy = GREEN_RIGHT_TURN_COUNT_RIGHT;
    uint8_t _lastPacked = 0;
    uint8_t _colorSeq = 1;
    uint8_t _fatenLeftColor = XIAO_COLOR_NONE_WIRE;
    uint8_t _fatenRightColor = XIAO_COLOR_NONE_WIRE;
    uint32_t _lastColorSendMs = 0;
    bool _hasColors = false;
    bool _hasRemotePlan = false;

    bool unpackDeploy(uint8_t packed, uint8_t& first, uint8_t& second) {
        if (packed < 11 || packed > 44) return false;
        first = packed / 10;
        second = packed % 10;
        return first >= 1 && first <= 4 && second >= 1 && second <= 4;
    }

    uint8_t colorToWire(uint8_t color) {
        return color <= K230_COLOR_YELLOW ? color : XIAO_COLOR_NONE_WIRE;
    }

    void sendColors() {
        Sensors::XIAO_link::send(XIAO_REG_FATEN_LEFT_COLOR, _fatenLeftColor);
        Sensors::XIAO_link::send(XIAO_REG_FATEN_RIGHT_COLOR, _fatenRightColor);
        Sensors::XIAO_link::send(XIAO_REG_FATEN_COLOR_SEQ, _colorSeq);
    }
}

void init() {
    _leftDeploy = GREEN_RIGHT_TURN_COUNT_LEFT;
    _rightDeploy = GREEN_RIGHT_TURN_COUNT_RIGHT;
    _lastPacked = 0;
    _colorSeq = 1;
    _hasColors = false;
    _hasRemotePlan = false;
}

void setFatenColors(uint8_t leftColor, uint8_t rightColor) {
    _fatenLeftColor = colorToWire(leftColor);
    _fatenRightColor = colorToWire(rightColor);
    _colorSeq = (_colorSeq >= 254) ? 1 : (uint8_t)(_colorSeq + 1);
    _hasColors = true;
    _lastColorSendMs = 0;
    sendColors();
}

void tick() {
    if (_hasColors && (millis() - _lastColorSendMs >= 250)) {
        _lastColorSendMs = millis();
        sendColors();
    }

    const uint8_t packed = Sensors::XIAO_link::get(XIAO_REG_FATEN_DEPLOY_PACKED);
    if (packed != _lastPacked) {
        uint8_t left = 0, right = 0;
        if (unpackDeploy(packed, left, right)) {
            _leftDeploy = left;
            _rightDeploy = right;
            _hasRemotePlan = true;
#if PRINT_STATE
            Serial.printf("[DeployPlan] Faten deploy packed=%u -> left=%u right=%u\n",
                          packed, _leftDeploy, _rightDeploy);
#endif
        }
        _lastPacked = packed;
    }
}

uint8_t leftDeployCount()  { return _leftDeploy; }
uint8_t rightDeployCount() { return _rightDeploy; }
bool    hasRemotePlan()    { return _hasRemotePlan; }

}  // namespace DeployPlan

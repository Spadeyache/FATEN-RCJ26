#include <Arduino.h>

// Minimal Kavosh Teensy-side example for the ordered deploy stream.
// Replace readKavoshLeftColor()/readKavoshRightColor() with Kavosh's real
// saved EVAC colors. Send COLOR_NONE_WIRE for an empty second gripper.

#define XIAO_SERIAL Serial3
#define XIAO_BAUD 4000000UL

#define REG_LEFT_COLOR    0x20
#define REG_RIGHT_COLOR   0x21
#define REG_COLOR_SEQ     0x22
#define REG_DEPLOY_PACKED 0x23
#define COLOR_NONE_WIRE   254

#define CD_BLACK  0
#define CD_BLUE   1
#define CD_GREEN  2
#define CD_ORANGE 3
#define CD_RED    4
#define CD_SILVER 5
#define CD_YELLOW 6

uint8_t regs[256];
uint8_t colorSeq = 1;
uint8_t lastPacked = 0;
uint8_t lastLeftColor = COLOR_NONE_WIRE;
uint8_t lastRightColor = COLOR_NONE_WIRE;
uint32_t lastSendMs = 0;

void sendReg(uint8_t reg, uint8_t value) {
    if (value == 255) value = 254;
    XIAO_SERIAL.write((uint8_t)255);
    XIAO_SERIAL.write(reg);
    XIAO_SERIAL.write(value);
}

void updateRegs() {
    while (XIAO_SERIAL.available() >= 3) {
        if (XIAO_SERIAL.peek() != 255) {
            XIAO_SERIAL.read();
            continue;
        }
        XIAO_SERIAL.read();
        const uint8_t reg = XIAO_SERIAL.read();
        const uint8_t value = XIAO_SERIAL.read();
        regs[reg] = value;
    }
}

uint8_t readKavoshLeftColor() {
    return CD_ORANGE;
}

uint8_t readKavoshRightColor() {
    return CD_SILVER;  // use COLOR_NONE_WIRE if Kavosh grabbed only one ball
}

void streamColors() {
    const uint8_t left = readKavoshLeftColor();
    const uint8_t right = readKavoshRightColor();
    if (left != lastLeftColor || right != lastRightColor) {
        lastLeftColor = left;
        lastRightColor = right;
        colorSeq = (colorSeq >= 254) ? 1 : (uint8_t)(colorSeq + 1);
    }
    sendReg(REG_LEFT_COLOR, left);
    sendReg(REG_RIGHT_COLOR, right);
    sendReg(REG_COLOR_SEQ, colorSeq);
}

bool unpackDeploy(uint8_t packed, uint8_t& first, uint8_t& second) {
    first = packed / 10;
    second = packed % 10;
    if (first < 1 || first > 4) return false;
    if (second > 4) return false;  // 0 means no second color
    return true;
}

void setup() {
    Serial.begin(115200);
    XIAO_SERIAL.begin(XIAO_BAUD);
    streamColors();
}

void loop() {
    updateRegs();

    const uint32_t now = millis();
    if ((uint32_t)(now - lastSendMs) >= 250) {
        lastSendMs = now;
        streamColors();
    }

    const uint8_t packed = regs[REG_DEPLOY_PACKED];
    if (packed != 0 && packed != lastPacked) {
        lastPacked = packed;
        uint8_t first = 0, second = 0;
        if (unpackDeploy(packed, first, second)) {
            Serial.printf("deploy packed=%u firstColorCount=%u secondColorCount=%u\n",
                          packed, first, second);
        }
    }
}

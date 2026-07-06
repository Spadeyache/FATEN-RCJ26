#include "FatenCoop.h"
#include "ColorAssign.h"
#include "../config/config.h"

#if COOP_ENABLE_FATEN_XIAO

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

namespace FatenCoop {

namespace {
    constexpr uint8_t MAGIC = 0xC7;
    constexpr uint8_t VERSION = 1;
    constexpr uint8_t ROLE_KAVOSH_COLORS = 1;
    constexpr uint8_t ROLE_FATEN_PLAN = 2;
    constexpr uint8_t LAYOUT_UNKNOWN = 255;

    // From kavosh-xiao-code/fatencode.txt: Faten sends to Kavosh.
    uint8_t KAVOSH_MAC[6] = {0xD8, 0x3B, 0xDA, 0x47, 0x24, 0x28};

    struct CoopPacket {
        uint8_t magic;
        uint8_t version;
        uint8_t role;
        uint8_t seq;
        uint8_t leftColor;
        uint8_t rightColor;
        uint8_t packedDeploy;
        uint8_t confidencePct;
        uint8_t layout;
        uint8_t crc;
    };

    uint8_t s_fatenLeft = ColorAssign::CD_NONE;
    uint8_t s_fatenRight = ColorAssign::CD_NONE;
    uint8_t s_fatenSeq = 0;
    uint8_t s_kavoshLeft = ColorAssign::CD_NONE;
    uint8_t s_kavoshRight = ColorAssign::CD_NONE;
    uint8_t s_kavoshSeq = 0;
    uint32_t s_lastKavoshMs = 0;
    uint32_t s_lastSendMs = 0;
    uint8_t s_lastFatenPacked = 0;
    uint8_t s_lastKavoshPacked = 0;
    uint8_t s_lastConfidencePct = 0;
    uint8_t s_lastLayout = LAYOUT_UNKNOWN;

    uint8_t crcPacket(const CoopPacket& p) {
        const uint8_t* b = reinterpret_cast<const uint8_t*>(&p);
        uint8_t x = 0;
        for (size_t i = 0; i < sizeof(CoopPacket) - 1; ++i) x ^= b[i];
        return x;
    }

    uint8_t wireToColor(uint8_t v) {
        return v <= ColorAssign::CD_YELLOW ? v : ColorAssign::CD_NONE;
    }

    uint8_t colorToWire(uint8_t v) {
        return v <= ColorAssign::CD_YELLOW ? v : XIAO_COLOR_NONE_WIRE;
    }

    uint8_t packDigits(uint8_t first, uint8_t second) {
        if (first > 4) first = 0;
        if (second > 4) second = 0;
        return (uint8_t)(first * 10 + second);
    }

    void computePlan() {
        const uint8_t colors[4] = {s_fatenLeft, s_fatenRight, s_kavoshLeft, s_kavoshRight};
        const ColorAssign::Result r = ColorAssign::compute(colors);
        s_lastFatenPacked = packDigits(r.number[0], r.number[1]);
        s_lastKavoshPacked = packDigits(r.number[2], r.number[3]);
        s_lastConfidencePct = (uint8_t)(r.confidence <= 0.0f ? 0.0f :
                                        r.confidence >= 1.0f ? 100.0f :
                                        r.confidence * 100.0f + 0.5f);
        s_lastLayout = r.layout;
    }

    void onDataRecv(const esp_now_recv_info*, const uint8_t* incomingData, int len) {
        if (len != (int)sizeof(CoopPacket)) return;
        CoopPacket p;
        memcpy(&p, incomingData, sizeof(p));
        if (p.magic != MAGIC || p.version != VERSION || p.role != ROLE_KAVOSH_COLORS) return;
        if (p.crc != crcPacket(p)) return;

        s_kavoshLeft = wireToColor(p.leftColor);
        s_kavoshRight = wireToColor(p.rightColor);
        s_kavoshSeq = p.seq;
        s_lastKavoshMs = millis();
        computePlan();
    }

    void addPeer() {
        esp_now_peer_info_t peer{};
        memcpy(peer.peer_addr, KAVOSH_MAC, sizeof(KAVOSH_MAC));
        peer.channel = COOP_ESPNOW_CHANNEL;
        peer.encrypt = false;
        esp_now_add_peer(&peer);
    }
}

void begin() {
    WiFi.mode(WIFI_STA);
    WiFi.STA.begin();
    esp_wifi_set_channel(COOP_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (esp_now_init() == ESP_OK) {
        esp_now_register_recv_cb(onDataRecv);
        addPeer();
    }
}

void tick(YacheEncodedSerial& teensy) {
    const uint8_t seq = teensy.get(XIAO_REG_FATEN_COLOR_SEQ);
    if (seq != 0 && seq != s_fatenSeq) {
        s_fatenSeq = seq;
        s_fatenLeft = wireToColor(teensy.get(XIAO_REG_FATEN_LEFT_COLOR));
        s_fatenRight = wireToColor(teensy.get(XIAO_REG_FATEN_RIGHT_COLOR));
        computePlan();
    }

    teensy.send(XIAO_REG_FATEN_DEPLOY_PACKED, s_lastFatenPacked);

    const uint32_t now = millis();
    if ((uint32_t)(now - s_lastKavoshMs) > COOP_STALE_MS) {
        s_kavoshLeft = ColorAssign::CD_NONE;
        s_kavoshRight = ColorAssign::CD_NONE;
        computePlan();
    }
    if ((uint32_t)(now - s_lastSendMs) < COOP_SEND_INTERVAL_MS) return;
    s_lastSendMs = now;

    CoopPacket p{};
    p.magic = MAGIC;
    p.version = VERSION;
    p.role = ROLE_FATEN_PLAN;
    p.seq = s_fatenSeq;
    p.leftColor = colorToWire(s_kavoshLeft);
    p.rightColor = colorToWire(s_kavoshRight);
    p.packedDeploy = s_lastKavoshPacked;
    p.confidencePct = s_lastConfidencePct;
    p.layout = s_lastLayout;
    p.crc = crcPacket(p);
    esp_now_send(KAVOSH_MAC, reinterpret_cast<uint8_t*>(&p), sizeof(p));
}

}  // namespace FatenCoop

#else

namespace FatenCoop {
void begin() {}
void tick(YacheEncodedSerial&) {}
}  // namespace FatenCoop

#endif

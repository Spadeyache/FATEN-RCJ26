#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// Kavosh XIAO bridge:
//   Kavosh Teensy -> XIAO: left/right grabbed colors, same ordered register protocol.
//   Kavosh XIAO -> Faten XIAO: colors over ESP-NOW.
//   Faten XIAO -> Kavosh XIAO: ordered deploy number, e.g. orange,silver -> 31.
//   Kavosh XIAO -> Teensy: packed deploy number on REG_DEPLOY_PACKED.

#define SERIAL_TEENSY_BAUD 4000000UL
#define SERIAL_TEENSY_RX_PIN D7
#define SERIAL_TEENSY_TX_PIN D6

#define ESPNOW_CHANNEL 1
#define SEND_INTERVAL_MS 200

#define REG_LEFT_COLOR    0x20
#define REG_RIGHT_COLOR   0x21
#define REG_COLOR_SEQ     0x22
#define REG_DEPLOY_PACKED 0x23
#define COLOR_NONE_WIRE   254

// From kavosh-xiao-code/kavoshcode.txt: Kavosh sends to Faten.
uint8_t FATEN_MAC[6] = {0xD8, 0x3B, 0xDA, 0x47, 0x1E, 0x08};

class EncodedSerial {
public:
    explicit EncodedSerial(HardwareSerial& serial) : s(serial) {
        for (int i = 0; i < 256; ++i) data[i] = 0;
    }

    void begin(unsigned long baud, int rxPin, int txPin) {
        s.begin(baud, SERIAL_8N1, rxPin, txPin);
    }

    void update() {
        while (s.available() >= 3) {
            if (s.peek() != 255) {
                s.read();
                continue;
            }
            s.read();
            const uint8_t reg = s.read();
            const uint8_t value = s.read();
            data[reg] = value;
        }
    }

    uint8_t get(uint8_t reg) const { return data[reg]; }

    void send(uint8_t reg, uint8_t value) {
        if (value == 255) value = 254;
        s.write((uint8_t)255);
        s.write(reg);
        s.write(value);
    }

private:
    HardwareSerial& s;
    uint8_t data[256];
};

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

static constexpr uint8_t MAGIC = 0xC7;
static constexpr uint8_t VERSION = 1;
static constexpr uint8_t ROLE_KAVOSH_COLORS = 1;
static constexpr uint8_t ROLE_FATEN_PLAN = 2;

EncodedSerial teensy(Serial1);

uint8_t kavoshLeft = COLOR_NONE_WIRE;
uint8_t kavoshRight = COLOR_NONE_WIRE;
uint8_t kavoshSeq = 0;
uint8_t lastPackedDeploy = 0;
uint32_t lastSendMs = 0;

uint8_t crcPacket(const CoopPacket& p) {
    const uint8_t* b = reinterpret_cast<const uint8_t*>(&p);
    uint8_t x = 0;
    for (size_t i = 0; i < sizeof(CoopPacket) - 1; ++i) x ^= b[i];
    return x;
}

void onDataRecv(const esp_now_recv_info*, const uint8_t* incomingData, int len) {
    if (len != (int)sizeof(CoopPacket)) return;
    CoopPacket p;
    memcpy(&p, incomingData, sizeof(p));
    if (p.magic != MAGIC || p.version != VERSION || p.role != ROLE_FATEN_PLAN) return;
    if (p.crc != crcPacket(p)) return;

    // This is the ordered stream Kavosh needs. Example:
    //   Kavosh sent orange,silver and Faten computed orange=3 silver=1 -> 31.
    lastPackedDeploy = p.packedDeploy;
}

void addPeer() {
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, FATEN_MAC, sizeof(FATEN_MAC));
    peer.channel = ESPNOW_CHANNEL;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
}

void sendColorsToFaten() {
    CoopPacket p{};
    p.magic = MAGIC;
    p.version = VERSION;
    p.role = ROLE_KAVOSH_COLORS;
    p.seq = kavoshSeq;
    p.leftColor = kavoshLeft;
    p.rightColor = kavoshRight;
    p.packedDeploy = 0;
    p.confidencePct = 0;
    p.layout = 255;
    p.crc = crcPacket(p);
    esp_now_send(FATEN_MAC, reinterpret_cast<uint8_t*>(&p), sizeof(p));
}

void setup() {
    Serial.begin(115200);
    teensy.begin(SERIAL_TEENSY_BAUD, SERIAL_TEENSY_RX_PIN, SERIAL_TEENSY_TX_PIN);

    WiFi.mode(WIFI_STA);
    WiFi.STA.begin();
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (esp_now_init() == ESP_OK) {
        esp_now_register_recv_cb(onDataRecv);
        addPeer();
    }
}

void loop() {
    teensy.update();

    const uint8_t seq = teensy.get(REG_COLOR_SEQ);
    if (seq != 0 && seq != kavoshSeq) {
        kavoshSeq = seq;
        kavoshLeft = teensy.get(REG_LEFT_COLOR);
        kavoshRight = teensy.get(REG_RIGHT_COLOR);
    }

    teensy.send(REG_DEPLOY_PACKED, lastPackedDeploy);

    const uint32_t now = millis();
    if ((uint32_t)(now - lastSendMs) >= SEND_INTERVAL_MS) {
        lastSendMs = now;
        sendColorsToFaten();
        Serial.printf("[KAVOSH] colors=%u,%u packed=%u\n", kavoshLeft, kavoshRight, lastPackedDeploy);
    }
}

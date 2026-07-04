// =============================================================================
//  kavosh serial test — Teensy 4.1 side (Serial7 / RX7 / TX7)
//
//  Counterpart to xiao_serial_test.ino. Mirrors the STS-imuMerge XIAO link
//  logic (a HardwareSerial at a fixed baud), but on Serial7 instead of Serial3.
//
//    Teensy 4.1 Serial7:  RX7 = pin 28,  TX7 = pin 29
//
//  Prints every line the XIAO sends, and sends its own "hello test" once a
//  second so you can verify both directions of the RX7/TX7 wiring.
//
//  WIRING
//    Teensy pin 28 (RX7) <--- XIAO D6 (TX)
//    Teensy pin 29 (TX7) ---> XIAO D7 (RX)
//    Teensy GND          <--> XIAO GND
// =============================================================================

#include <Arduino.h>

#define LINK        Serial7      // RX7 = pin 28, TX7 = pin 29
#define LINK_BAUD   115200       // must match the XIAO side (bump both to 4000000 for the real link)

static unsigned long lastSend = 0;

void setup() {
    Serial.begin(115200);        // USB debug console
    LINK.begin(LINK_BAUD);
    Serial.println("[Teensy] serial test up (Serial7)");
}

void loop() {
    // 1) print whatever the XIAO sent
    while (LINK.available()) {
        String line = LINK.readStringUntil('\n');
        line.trim();
        if (line.length()) {
            Serial.print("[Teensy] got from XIAO: ");
            Serial.println(line);
        }
    }

    // 2) send our own hello line once a second
    if (millis() - lastSend >= 1000) {
        LINK.println("hello test from Teensy");
        Serial.println("[Teensy] sent: hello test from Teensy");
        lastSend = millis();
    }
}

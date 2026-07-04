// =============================================================================
//  kavosh serial test — XIAO ESP32-S3 side
//
//  Minimal "hello test" over the same UART link used by XIAOdev, but wired to
//  the Teensy's Serial7 (RX7 = pin 28, TX7 = pin 29) instead of Serial3.
//
//  Logic mirrors XIAOdev: the Teensy link lives on Serial1 with the pins from
//  hardware_config.h (D6 = TX, D7 = RX). This sketch just sends a text line
//  once a second and prints whatever comes back, so you can confirm the RX/TX
//  wiring works before layering the real yacheEncodedSerial protocol on top.
//
//  WIRING
//    XIAO D6 (TX) ---> Teensy pin 28 (RX7)
//    XIAO D7 (RX) <--- Teensy pin 29 (TX7)
//    XIAO GND     <--> Teensy GND
// =============================================================================

#include <Arduino.h>

// ── Serial link to Teensy (same pins as XIAOdev/src/config/hardware_config.h) ─
#define LINK_BAUD    115200      // faten link runs at 4000000; 115200 is easy to probe.
#define LINK_RX_PIN  D7          // XIAO RX  <- Teensy TX7 (pin 29)
#define LINK_TX_PIN  D6          // XIAO TX  -> Teensy RX7 (pin 28)

HardwareSerial& Teensy = Serial1;   // XIAOdev uses Serial1 for the Teensy link

static unsigned long lastSend = 0;

void setup() {
    Serial.begin(115200);           // USB debug console
    Teensy.begin(LINK_BAUD, SERIAL_8N1, LINK_RX_PIN, LINK_TX_PIN);
    Serial.println("[XIAO] serial test up");
}

void loop() {
    // 1) send a hello line once a second
    if (millis() - lastSend >= 1000) {
        Teensy.println("hello test from XIAO");
        Serial.println("[XIAO] sent: hello test from XIAO");
        lastSend = millis();
    }

    // 2) print anything the Teensy sends back
    while (Teensy.available()) {
        String line = Teensy.readStringUntil('\n');
        line.trim();
        if (line.length()) {
            Serial.print("[XIAO] got from Teensy: ");
            Serial.println(line);
        }
    }
}

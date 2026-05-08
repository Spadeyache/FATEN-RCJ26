// Teensy 4.1 UART echo test
// - USB Serial: for debug to your PC
// - Serial1: hardware UART pins 0 (RX1) and 1 (TX1) on Teensy 4.1
//
// Wiring:
//   Teensy RX1 (pin 0)  <- K230 TX
//   Teensy TX1 (pin 1)  -> K230 RX
//   GND shared

#include <Arduino.h>

#define _74HTC126EN 2

static const uint32_t BAUD = 115200;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) { } // allow time for Serial Monitor

  Serial5.begin(BAUD);
  
  // Enable 74HCT126 - to prevent error
  pinMode(_74HTC126EN, OUTPUT);
  digitalWrite(_74HTC126EN, HIGH);

  Serial.println("Teensy UART echo test start");
  Serial.print("Serial5 baud = ");
  Serial.println(BAUD);
}

void loop() {
  // If data arrives from K230, echo it back and also print to USB Serial.
  while (Serial5.available() > 0) {
    int c = Serial5.read();
    if (c >= 0) {
      // Echo back to K230
      Serial5.write((char)c);

      // Also show on PC
      Serial.write((char)c);
    }
  }
}
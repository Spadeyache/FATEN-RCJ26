// Teensy 4.1 UART echo test
// - USB Serial: for debug to your PC
// - Serial8: hardware UART pins 34 (RX8) and 35 (TX8) on Teensy 4.1
//
// Wiring:
//   Teensy RX8 (pin 34)  <- K230 TX
//   Teensy TX8 (pin 35)  -> K230 RX
//   GND shared

#include <Arduino.h>

#define _74HTC126EN 2

static const uint32_t BAUD = 115200;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) { } // allow time for Serial Monitor

  Serial8.begin(BAUD);
  
  // Enable 74HCT126 - to prevent error
  pinMode(_74HTC126EN, OUTPUT);
  digitalWrite(_74HTC126EN, HIGH);

  Serial.println("Teensy UART echo test start");
  Serial.print("Serial8 baud = ");
  Serial.println(BAUD);
}

void loop() {
  // If data arrives from K230, echo it back and also print to USB Serial.
  while (Serial8.available() > 0) {
    int c = Serial8.read();
    if (c >= 0) {
      // Echo back to K230
      Serial8.write((char)c);

      // Also show on PC
      Serial.write((char)c);
    }
  }
}
/*
27 Feb 2026
This code tested the touch and conduct pins.with start switch.
*/

#include "Arduino.h"

#define _startSW 34

#define _touchPin0 33
#define _touchPin1 35
#define _conductPin0 5
#define _conductPin1 6

volatile bool touch0 = false;
volatile bool touch1 = false;
volatile bool conduct0 = false;
volatile bool conduct1 = false;

void setup() {
  Serial.begin(115200);

  pinMode(_startSW, INPUT_PULLUP);

  pinMode(_touchPin0, INPUT_PULLUP);
  pinMode(_touchPin1, INPUT_PULLUP);
  pinMode(_conductPin0, INPUT_PULLUP);
  pinMode(_conductPin1, INPUT_PULLUP);

  Serial.println("waiting for startSW");
  while(digitalRead(_startSW) == HIGH){delay(20);}
  delay(10);
  Serial.println("System Ready.");  
}

void loop() {
// HIGH(1) is OFF and LOW(0) is Touching 
  updateBinInputs();


  Serial.print("T0: ");
  Serial.print(touch0);
  Serial.print("|T1: ");
  Serial.print(touch1);
  Serial.print("|C0: ");
  Serial.print(conduct0);
  Serial.print("|C1: ");
  Serial.println(conduct1);

  delay(50);
}

FASTRUN void updateBinInputs(){
  touch0 = digitalRead(_touchPin0);
  touch1 = digitalRead(_touchPin1);
  conduct0 = digitalRead(_conductPin0);
  conduct1 = digitalRead(_conductPin1);
}
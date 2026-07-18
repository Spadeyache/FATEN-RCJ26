  // XIAO ESP32-S3 UART Sender/Receiver
  #include <Arduino.h>

  void setup() {
      Serial.begin(115200); 
      // Force pins to D7 (RX) and D6 (TX)
      Serial1.begin(115200, SERIAL_8N1, D7, D6); 
      
      pinMode(LED_BUILTIN, OUTPUT);
      delay(2000);
      Serial.println("XIAO S3: High-Frequency Test Start");
  }
  
  void loop() {
      // Send a message every 500ms
      Serial1.println("DATA_FROM_XIAO");
      Serial.println("Sending to Teensy...");
      
      // Blink LED to show code is alive
      digitalWrite(LED_BUILTIN, LOW); // ESP32 LED is active LOW
      delay(100);
      digitalWrite(LED_BUILTIN, HIGH);
      delay(400);
  
      while(Serial1.available()) {
          Serial.print("Teensy says: ");
          Serial.println(Serial1.readStringUntil('\n'));
      }
  }
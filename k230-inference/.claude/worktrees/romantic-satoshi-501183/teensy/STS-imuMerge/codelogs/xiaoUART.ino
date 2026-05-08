// Teensy 4.1 UART4 Sender/Receiver

void setup() {
    Serial.begin(115200);  // USB Monitor
    Serial4.begin(115200); // Pins 16 and 17
    delay(1000);
    Serial.println("Teensy Online. Waiting for XIAO...");
}
  
  void loop() {
    // Check if data is coming FROM the XIAO
    if (Serial4.available()) {
        char c = Serial4.read();
        Serial.print(c); // Print it to your PC screen
  
        Serial4.println("Teensy received your message!");
        Serial.println("Sent: Confirmation to XIAO");
        delay(100);
    }
}
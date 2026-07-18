#include "YacheEncodedSerial.h"

YacheEncodedSerial xiao(Serial3);

uint8_t command = 0;
uint8_t gain = 0;

void setup() {
    xiao.begin(115200);
}

void loop() {
    xiao.update(); // recive and save data
    
    // Send Data
    xiao.send(0x01, (uint8_t)1); // 0-254 are allowed. 255 is used for synchronization
    xiao.send(0x02, (uint8_t)150);
    delay(10);

    //  Read Data
    command = xiao.get(0x01);  // 1
    gain    = xiao.get(0x02);  // 150

    // Debug print log every 500ms
    static unsigned long lastPrint = 0;
    if (millis() - lastPrint > 500) {
        Serial.print("Cmd: "); Serial.print(command);
        Serial.print(" | Gain: "); Serial.println(gain);
        
        lastPrint = millis();
    }
}
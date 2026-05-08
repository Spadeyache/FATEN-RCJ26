#include <Arduino.h>
#include "camera_config.h"
#include "vision.h"
#include "wifi_config.h"

void setup() {
    Serial.begin(115200);
    while (!Serial) {
        delay(10); // Wait for serial port to connect
    }

    Serial.println("\n=== XIAO ESP32-S3 Sense Vision System ===");

    // Initialize camera
    if (!Camera_Init()) {
        Serial.println("Camera initialization failed!");
        delay(100);
    }
    else{Serial.println("Camera initialized successfully");}
    // Initialize WiFi
    Serial.printf("Connecting to %s ", WIFI_SSID);
    if (WiFi_Init()) {
        Serial.printf("Connected! IP address: %s\n", WiFi.localIP().toString().c_str());
        delay(100);
    } else {
        Serial.println("Connection failed or timed out");
    }
    
}

void loop() {
    static unsigned long lastTime = 0;
    if (millis() - lastTime < 100) {
        return; // Run every 100ms
    }
    lastTime = millis();

    // Capture frame
    camera_fb_t* fb = Camera_Grab();
    if (!fb) {
        Serial.println("Camera capture failed");
        return;
    }

    // Process frame for vision data
    FrameResult result = Vision_Process(fb);

    // Print results
    Vision_Print(result);

    // Return frame buffer
    Camera_Return(fb);

    // TODO: Add line steering logic here based on result.line.offset
    // TODO: Add color reaction logic here based on result.color flags
}
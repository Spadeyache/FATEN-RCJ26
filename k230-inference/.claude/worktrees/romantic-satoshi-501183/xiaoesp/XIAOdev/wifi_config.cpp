#include "wifi_config.h"
#include <Arduino.h>

bool WiFi_Init() {
    // Set WiFi to station mode and disconnect from any previous AP
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    // Attempt to connect to WiFi network
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    // Wait for connection or timeout (optimized for fast initialization)
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - startAttemptTime < WIFI_TIMEOUT_MS) {
        // Minimal delay to prevent watchdog timeout but keep initialization fast
        delay(10);
    }

    return WiFi.status() == WL_CONNECTED;
}
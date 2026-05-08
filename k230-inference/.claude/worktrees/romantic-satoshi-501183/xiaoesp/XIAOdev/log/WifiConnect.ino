#include <WiFi.h>

const char *ssid = "Spadeyache32";
const char *password = "pkxc9kvozip2";

void scanNetworks() {
  Serial.println("\n--- Scanning for WiFi Networks ---");
  int networkCount = WiFi.scanNetworks();

  if (networkCount == 0) {
    Serial.println("No networks found!");
  } else {
    Serial.print(networkCount);
    Serial.println(" networks found:");

    bool targetFound = false;
    for (int i = 0; i < networkCount; i++) {
      String foundSSID = WiFi.SSID(i);
      int rssi = WiFi.RSSI(i);
      String encryption = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "Open" : "Secured";

      Serial.print("  [");
      Serial.print(i + 1);
      Serial.print("] ");
      Serial.print(foundSSID);
      Serial.print("  (");
      Serial.print(rssi);
      Serial.print(" dBm)  ");
      Serial.println(encryption);

      if (foundSSID == ssid) {
        targetFound = true;
        Serial.println("      ^^^ TARGET NETWORK FOUND ^^^");
      }
    }

    if (!targetFound) {
      Serial.print("\n! Target SSID \"");
      Serial.print(ssid);
      Serial.println("\" was NOT found in scan.");
      Serial.println("  Check spelling, or it may be 5GHz / hidden.");
    }
  }

  WiFi.scanDelete(); // Free memory
  Serial.println("----------------------------------\n");
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("\n--- XIAO ESP32-S3 WiFi Test ---");

  WiFi.mode(WIFI_STA);
  scanNetworks(); // Scan first so you can see if SSID is visible

  Serial.print("Connecting to: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Signal Strength (RSSI): ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println("\nFailed to connect after 20 attempts.");
    Serial.println("Check password, or network may require browser login (captive portal).");
  }
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Lost connection. Reconnecting...");
    WiFi.begin(ssid, password);
  }
  delay(10000);
}
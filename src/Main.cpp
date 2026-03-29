#include "OtaUpdate.h"
#include "RgbLed.h"
#include <Preferences.h>
#include "TwaiTaskBased.h"
#include "Globals.h"
#include <Arduino.h>
#include <driver/gpio.h>

RgbLed statusLed(8);
OtaUpdate otaUpdate(statusLed, 180000, "", "");

bool currentTxErrors = false;

// WiFi credential reception state (CAN ID 0x01 protocol)
bool wifiConfigInProgress = false;
uint8_t wifiSsidBuffer[33];       // Max 32 chars + null
uint8_t wifiPasswordBuffer[64];   // Max 63 chars + null
uint8_t wifiSsidLen = 0;          // Expected total SSID length
uint8_t wifiPasswordLen = 0;      // Expected total password length
uint8_t wifiSsidReceived = 0;     // Bytes received so far
uint8_t wifiPasswordReceived = 0;

void saveWifiCredentials(const char* ssid, const char* password) {
  Preferences prefs;
  prefs.begin("wifi", false);  // read-write
  prefs.putString("ssid", ssid);
  prefs.putString("password", password);
  prefs.end();
  debugf("[WiFi] Credentials saved to NVS (SSID: %s)\n", ssid);
}

void handleWifiConfigMessage(const twai_message_t &msg) {
  uint8_t msgType = msg.data[0];

  switch (msgType) {
    case 0x01: {  // Start message
      wifiSsidLen = msg.data[1];
      wifiPasswordLen = msg.data[2];
      wifiSsidReceived = 0;
      wifiPasswordReceived = 0;
      memset(wifiSsidBuffer, 0, sizeof(wifiSsidBuffer));
      memset(wifiPasswordBuffer, 0, sizeof(wifiPasswordBuffer));
      wifiConfigInProgress = true;
      debugf("[WiFi] Config start: SSID len=%d, Password len=%d\n", wifiSsidLen, wifiPasswordLen);
      break;
    }

    case 0x02: {  // SSID chunk
      if (!wifiConfigInProgress) break;
      uint8_t dataBytes = msg.data_length_code - 2;
      uint8_t remaining = wifiSsidLen - wifiSsidReceived;
      if (dataBytes > remaining) dataBytes = remaining;
      if (wifiSsidReceived + dataBytes <= 32) {
        memcpy(wifiSsidBuffer + wifiSsidReceived, &msg.data[2], dataBytes);
        wifiSsidReceived += dataBytes;
      }
      break;
    }

    case 0x03: {  // Password chunk
      if (!wifiConfigInProgress) break;
      uint8_t dataBytes = msg.data_length_code - 2;
      uint8_t remaining = wifiPasswordLen - wifiPasswordReceived;
      if (dataBytes > remaining) dataBytes = remaining;
      if (wifiPasswordReceived + dataBytes <= 63) {
        memcpy(wifiPasswordBuffer + wifiPasswordReceived, &msg.data[2], dataBytes);
        wifiPasswordReceived += dataBytes;
      }
      break;
    }

    case 0x04: {  // End message with checksum
      if (!wifiConfigInProgress) break;
      wifiConfigInProgress = false;

      uint8_t checksum = 0;
      for (uint8_t i = 0; i < wifiSsidReceived; i++) checksum ^= wifiSsidBuffer[i];
      for (uint8_t i = 0; i < wifiPasswordReceived; i++) checksum ^= wifiPasswordBuffer[i];

      if (checksum == msg.data[1] && wifiSsidReceived == wifiSsidLen && wifiPasswordReceived == wifiPasswordLen) {
        wifiSsidBuffer[wifiSsidReceived] = '\0';
        wifiPasswordBuffer[wifiPasswordReceived] = '\0';
        saveWifiCredentials((const char*)wifiSsidBuffer, (const char*)wifiPasswordBuffer);
      } else {
        debugf("[WiFi] Config failed: checksum %s, SSID %d/%d bytes, Password %d/%d bytes\n",
               (checksum == msg.data[1]) ? "OK" : "MISMATCH",
               wifiSsidReceived, wifiSsidLen, wifiPasswordReceived, wifiPasswordLen);
      }
      break;
    }
  }
}

void onCanRx(const twai_message_t &msg) {
  if (msg.identifier == 0) {
    Serial.println("Got message for someone to update");
    char updateForHostName[14];
    String currentHostName = otaUpdate.getHostName();
    sprintf(updateForHostName, "esp32c6-%X%X%X", msg.data[0], msg.data[1],
            msg.data[2]);
    Serial.println(updateForHostName);
    if (currentHostName.equals(updateForHostName)) {
      debugln("[OTA] Hostname matched - reading WiFi credentials from NVS");
      Preferences prefs;
      prefs.begin("wifi", true);  // read-only
      String ssid = prefs.getString("ssid", "");
      String password = prefs.getString("password", "");
      prefs.end();

      if (ssid.length() > 0 && password.length() > 0) {
        debugf("[OTA] Using stored WiFi credentials (SSID: %s)\n", ssid.c_str());
        OtaUpdate ota(statusLed, 180000, ssid.c_str(), password.c_str());
        ota.waitForOta();
        debugln("[OTA] OTA mode exited - resuming normal operation");
      } else {
        debugln("[OTA] ERROR: No WiFi credentials in NVS - cannot start OTA");
      }
    }
  }
  else if (msg.identifier == 0x01) {
    handleWifiConfigMessage(msg);
  }
}

void onCanTx(bool ok) {
  currentTxErrors = !ok;
  debugln(ok ? "TX OK" : "TX FAIL");
}

void setup() {
  Serial.begin(115200);
#if DEBUG == 0
  Serial.println(
      "Debugging is disabled no more output will be written here...");
#else
  Serial.println("Debugging started");
#endif
  statusLed.begin();
  statusLed.setBrightnessPercent(1);
  TwaiTaskBased::onReceive(onCanRx);
  TwaiTaskBased::onTransmit(onCanTx);
  TwaiTaskBased::begin(GPIO_NUM_15, GPIO_NUM_14, 500000, TWAI_MODE_NO_ACK);
  statusLed.green();
  debugln("Setup done");
}

void loop() {

}

// Device side: Arduino entry points, WiFi and HTTPS for the ESP32.

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "app.h"
#include "platform.h"
#include "settings.h"

std::string platformNetworkName() { return "WiFi " WIFI_SSID; }

void platformBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  // The clock sets itself from the internet once WiFi has joined.
  configTzTime(TIMEZONE, "pool.ntp.org", "time.google.com");
}

bool platformOnline() { return WiFi.status() == WL_CONNECTED; }

int platformGithubGet(const std::string &path, JsonDocument &doc, const JsonDocument &filter) {
  WiFiClientSecure client;
  // The server certificate is not verified. Only public, read-only data is
  // requested and no credentials are sent.
  client.setInsecure();

  HTTPClient http;
  http.useHTTP10(true);  // no chunked encoding, so the body can be parsed as a stream
  http.setTimeout(15000);
  if (!http.begin(client, ("https://api.github.com" + path).c_str())) return -1;
  http.addHeader("Accept", "application/vnd.github+json");
  http.setUserAgent("esp32-github-activity-display");

  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    DeserializationError err =
        deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    if (err) {
      Serial.printf("JSON error for %s: %s\n", path.c_str(), err.c_str());
      code = -2;
    }
  } else if (code < 0) {
    code = -1;
  }
  http.end();
  return code;
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  appSetup();
}

void loop() { appLoop(); }

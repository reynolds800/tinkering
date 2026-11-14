// Device side: Arduino entry points, WiFi and HTTPS for the ESP32.

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <time.h>

#include "app.h"
#include "platform.h"
#include "settings.h"

static const uint16_t CHAT_PORT = 47800;
static WiFiUDP chatSocket;
static bool chatListening = false;

std::string platformDeviceName() { return DEVICE_NAME; }

// The socket can only be opened once WiFi has joined.
static bool chatReady() {
  if (WiFi.status() != WL_CONNECTED) {
    if (chatListening) chatSocket.stop();
    chatListening = false;
    return false;
  }
  if (!chatListening) chatListening = chatSocket.begin(CHAT_PORT);
  return chatListening;
}

bool platformChatSend(const std::string &packet) {
  if (!chatReady()) return false;
  if (!chatSocket.beginPacket(IPAddress(255, 255, 255, 255), CHAT_PORT)) return false;
  chatSocket.write((const uint8_t *)packet.data(), packet.size());
  return chatSocket.endPacket();
}

bool platformChatReceive(std::string &packet) {
  if (!chatReady()) return false;
  int size = chatSocket.parsePacket();
  if (size <= 0) return false;
  char buf[512];
  int n = chatSocket.read(buf, sizeof(buf));
  chatSocket.flush();  // discard anything beyond the buffer
  if (n <= 0) return false;
  packet.assign(buf, n);
  return true;
}

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

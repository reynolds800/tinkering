// Device side: Arduino entry points, WiFi and HTTPS for the ESP32.

#include <Arduino.h>
#include <HTTPClient.h>
#include <Preferences.h>
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

// Addresses packets have arrived from, so replies can be sent to them directly.
static const int MAX_PEERS = 8;
static IPAddress chatPeers[MAX_PEERS];
static int chatPeerCount = 0;

static void rememberPeer(IPAddress address) {
  if (address == WiFi.localIP()) return;
  for (int i = 0; i < chatPeerCount; i++) {
    if (chatPeers[i] == address) return;
  }
  if (chatPeerCount < MAX_PEERS) chatPeers[chatPeerCount++] = address;
}

static bool sendTo(IPAddress address, const std::string &packet) {
  if (!chatSocket.beginPacket(address, CHAT_PORT)) return false;
  chatSocket.write((const uint8_t *)packet.data(), packet.size());
  return chatSocket.endPacket();
}

std::string platformDeviceName() { return DEVICE_NAME; }

// The socket can only be opened once WiFi has joined.
static bool chatReady() {
  if (WiFi.status() != WL_CONNECTED) {
    if (chatListening) chatSocket.stop();
    chatListening = false;
    return false;
  }
  if (!chatListening) {
    chatListening = chatSocket.begin(CHAT_PORT);
    Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
  }
  return chatListening;
}

bool platformChatSend(const std::string &packet) {
  if (!chatReady()) return false;
  bool sent = sendTo(IPAddress(255, 255, 255, 255), packet);
  for (int i = 0; i < chatPeerCount; i++) sendTo(chatPeers[i], packet);
  return sent;
}

bool platformChatReceive(std::string &packet) {
  if (!chatReady()) return false;
  int size = chatSocket.parsePacket();
  if (size <= 0) return false;
  char buf[512];
  int n = chatSocket.read(buf, sizeof(buf));
  chatSocket.flush();  // discard anything beyond the buffer
  if (n <= 0) return false;
  rememberPeer(chatSocket.remoteIP());
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

std::string platformLoadSetting(const char *key) {
  Preferences store;
  store.begin("epulse", true);
  // Asking for a setting that was never saved logs an error, so check first.
  std::string value = store.isKey(key) ? store.getString(key, "").c_str() : "";
  store.end();
  return value;
}

void platformSaveSetting(const char *key, const std::string &value) {
  Preferences store;
  store.begin("epulse", false);
  store.putString(key, value.c_str());
  store.end();
}

int platformHttpGet(const std::string &url,
                    const std::function<bool(const char *data, size_t size)> &onData) {
  WiFiClientSecure client;
  // The server certificate is not verified. Only public pages are requested
  // and no credentials are sent.
  client.setInsecure();

  HTTPClient http;
  http.useHTTP10(true);  // no chunked encoding, so the body arrives as plain bytes
  http.setTimeout(15000);
  if (!http.begin(client, url.c_str())) return -1;
  http.setUserAgent("esp32-github-activity-display");

  int code = http.GET();
  if (code < 0) code = -1;
  if (code == HTTP_CODE_OK) {
    WiFiClient *stream = http.getStreamPtr();
    int remaining = http.getSize();  // -1 when the server does not say
    static char buf[1024];
    uint32_t lastData = millis();
    while (remaining != 0 && (http.connected() || stream->available())) {
      int available = stream->available();
      if (available <= 0) {
        if (millis() - lastData > 15000) break;  // stalled
        delay(2);
        continue;
      }
      int n = stream->readBytes(buf, min(available, (int)sizeof(buf)));
      if (n <= 0) break;
      lastData = millis();
      if (remaining > 0) remaining -= n;
      if (!onData(buf, n)) break;
    }
  }
  http.end();
  return code;
}

bool platformOnline() { return WiFi.status() == WL_CONNECTED; }

int platformJsonGet(const std::string &url, JsonDocument &doc, const JsonDocument &filter) {
  WiFiClientSecure client;
  // The server certificate is not verified. Only public, read-only data is
  // requested and no credentials are sent.
  client.setInsecure();

  HTTPClient http;
  http.useHTTP10(true);  // no chunked encoding, so the body can be parsed as a stream
  http.setTimeout(15000);
  if (!http.begin(client, url.c_str())) return -1;
  http.setUserAgent("esp32-github-activity-display");

  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    DeserializationError err =
        deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    if (err) {
      Serial.printf("JSON error for %s: %s\n", url.c_str(), err.c_str());
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

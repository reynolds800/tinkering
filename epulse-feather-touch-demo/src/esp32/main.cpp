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

// The battery is wired to this pin through two resistors that scale its
// voltage down to what the pin can measure, as in ThingPulse's test firmware:
// https://github.com/ThingPulse/esp32-epulse-feather-testbed
static const int BATTERY_PIN = 35;
static const float BATTERY_SCALE = (2.2f + 4.7f) / 4.7f;

// A lithium battery's voltage falls as it empties, but not evenly: it drops
// quickly at first, holds steady through the middle, then falls away at the
// end. This table follows that curve.
static int percentForVolts(float volts) {
  static const struct {
    float volts;
    int percent;
  } CURVE[] = {{4.20f, 100}, {4.15f, 95}, {4.11f, 90}, {4.08f, 85}, {4.02f, 80}, {3.98f, 75},
               {3.95f, 70},  {3.91f, 65}, {3.87f, 60}, {3.85f, 55}, {3.84f, 50}, {3.82f, 45},
               {3.80f, 40},  {3.79f, 35}, {3.77f, 30}, {3.75f, 25}, {3.73f, 20}, {3.71f, 15},
               {3.69f, 10},  {3.61f, 5},  {3.27f, 0}};
  const int n = sizeof(CURVE) / sizeof(CURVE[0]);
  if (volts >= CURVE[0].volts) return 100;
  for (int i = 1; i < n; i++) {
    if (volts >= CURVE[i].volts) {
      float span = CURVE[i - 1].volts - CURVE[i].volts;
      float part = (volts - CURVE[i].volts) / span;
      return CURVE[i].percent + (int)lroundf(part * (CURVE[i - 1].percent - CURVE[i].percent));
    }
  }
  return 0;
}

int platformBatteryPercent() {
  static float volts = 0;
  static bool measured = false;
  static uint32_t lastRead = 0, lastLog = 0;

  // The voltage changes slowly, so it is only measured every few seconds.
  if (!measured || millis() - lastRead > 5000) {
    uint32_t total = 0;
    for (int i = 0; i < 16; i++) total += analogReadMilliVolts(BATTERY_PIN);
    float reading = total / 16.0f / 1000.0f * BATTERY_SCALE;
    // Readings jump about, especially while WiFi is busy, so each new one
    // only nudges the figure in use.
    volts = measured ? volts * 0.8f + reading * 0.2f : reading;
    lastRead = millis();
    if (!measured || millis() - lastLog > 300000) {
      Serial.printf("Battery: %.2f V, %d%%\n", volts, percentForVolts(volts));
      lastLog = millis();
    }
    measured = true;
  }
  if (volts < 2.5f) return -1;  // nothing sensible on the pin
  return percentForVolts(volts);
}

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
  http.setUserAgent("epulse-feather-apps/1.0");

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

// A Stream that keeps whatever is written to it, up to a limit.
class Collector : public Stream {
public:
  Collector(std::string &out, size_t limit) : _out(out), _limit(limit) {}
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t *data, size_t size) override {
    if (_out.size() + size > _limit) return 0;
    _out.append((const char *)data, size);
    return size;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}

private:
  std::string &_out;
  size_t _limit;
};

int platformDownload(const std::string &url, std::string &body) {
  // Kept between calls so that the connection, and the slow secure
  // handshake that sets it up, can be reused for the next download.
  static WiFiClientSecure client;
  static HTTPClient http;
  static bool configured = false;
  if (!configured) {
    // The server certificate is not verified. Only public files are
    // requested and no credentials are sent.
    client.setInsecure();
    http.setReuse(true);
    http.setTimeout(15000);
    http.setUserAgent("epulse-feather-apps/1.0");
    configured = true;
  }

  body.clear();
  if (!http.begin(client, url.c_str())) return -1;
  int code = http.GET();
  if (code < 0) {
    http.end();
    client.stop();
    return -1;
  }
  if (code == HTTP_CODE_OK) {
    Collector collector(body, 300000);
    if (http.writeToStream(&collector) < 0) code = -1;
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
  http.setUserAgent("epulse-feather-apps/1.0");

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

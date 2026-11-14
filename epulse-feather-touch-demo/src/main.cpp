// GitHub activity display for the ThingPulse ePulse Feather with the 3.5"
// ILI9488 display and FT6236 capacitive touch. Shows a user's recent public
// events; tap the screen to refresh.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <algorithm>
#include <time.h>

#if __has_include("config.h")
#include "config.h"
#else
#error "Copy include/config.example.h to include/config.h and fill in your WiFi details"
#endif

static const uint8_t TFT_ROTATION = 2;  // portrait, as in the ThingPulse sample

static const uint8_t FT6236_ADDR = 0x38;
static const uint8_t FT6236_REG_NUM_TOUCHES = 0x02;
static const uint8_t FT6236_REG_THRESHOLD = 0x80;

static const int MARGIN = 12;
static const int HEADER_H = 64;
static const int ROW_H = 50;
static const int MAX_EVENTS = 8;
static const int FOOTER_Y = 469;
static const uint16_t COLOUR_DIM = 0x8410;  // mid grey

struct Event {
  String text;
  String repo;
  time_t when;
  uint16_t colour;
};

TFT_eSPI tft;
bool touchOk = false;

Event events[MAX_EVENTS];
int eventCount = 0;
String profileLine;
String statusLine = "Starting";
bool statusIsError = false;

// ---------- touch ----------

bool touchBegin() {
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  Wire.beginTransmission(FT6236_ADDR);
  Wire.write(FT6236_REG_THRESHOLD);
  Wire.write(40);  // sensitivity; lower = more sensitive
  return Wire.endTransmission() == 0;
}

bool touched() {
  if (!touchOk) return false;
  Wire.beginTransmission(FT6236_ADDR);
  Wire.write(FT6236_REG_NUM_TOUCHES);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(FT6236_ADDR, (uint8_t)1) != 1) return false;
  uint8_t touches = Wire.read();
  return touches == 1 || touches == 2;
}

// ---------- text helpers ----------

// The built-in fonts only cover ASCII.
String ascii(const char *s) {
  String out;
  if (!s) return out;
  for (; *s; s++) {
    uint8_t c = (uint8_t)*s;
    if (c >= 32 && c < 127) {
      out += (char)c;
    } else if ((c & 0xC0) != 0x80) {  // one '?' per multi-byte character
      out += '?';
    }
  }
  return out;
}

String fit(String s, int maxWidth, uint8_t font) {
  if (tft.textWidth(s, font) <= maxWidth) return s;
  while (s.length() > 1 && tft.textWidth(s + "..", font) > maxWidth) {
    s.remove(s.length() - 1);
  }
  return s + "..";
}

String capitalised(String s) {
  if (s.length()) s.setCharAt(0, toupper(s[0]));
  return s;
}

time_t parseIsoUtc(const char *iso) {
  struct tm t = {};
  if (!iso || sscanf(iso, "%d-%d-%dT%d:%d:%d", &t.tm_year, &t.tm_mon, &t.tm_mday,
                     &t.tm_hour, &t.tm_min, &t.tm_sec) != 6) {
    return 0;
  }
  t.tm_year -= 1900;
  t.tm_mon -= 1;
  return mktime(&t);  // local timezone is UTC, see configTime()
}

bool clockIsSet() { return time(nullptr) > 1600000000; }

String timeAgo(time_t when) {
  if (!when || !clockIsSet()) return "";
  long secs = time(nullptr) - when;
  if (secs < 60) return "now";
  if (secs < 3600) return String(secs / 60) + "m";
  if (secs < 86400) return String(secs / 3600) + "h";
  return String(secs / 86400) + "d";
}

// ---------- drawing ----------

void drawFooter() {
  tft.fillRect(0, FOOTER_Y - 2, tft.width(), tft.height() - FOOTER_Y + 2, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(statusIsError ? TFT_RED : COLOUR_DIM, TFT_BLACK);
  tft.drawString(fit(statusLine, tft.width() - 2 * MARGIN, 1), MARGIN, FOOTER_Y, 1);
}

void setStatus(const String &message, bool isError = false) {
  statusLine = message;
  statusIsError = isError;
  Serial.println(message);
  drawFooter();
}

void drawScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(fit("@" GITHUB_USER, tft.width() - 2 * MARGIN, 4), MARGIN, 8, 4);
  tft.setTextColor(COLOUR_DIM, TFT_BLACK);
  tft.drawString(fit(profileLine.length() ? profileLine : String("GitHub activity"),
                     tft.width() - 2 * MARGIN, 2),
                 MARGIN, 38, 2);
  tft.drawFastHLine(0, HEADER_H - 3, tft.width(), TFT_DARKGREY);

  const int textX = 30;
  const int agoW = 36;
  for (int i = 0; i < eventCount; i++) {
    const Event &e = events[i];
    int y = HEADER_H + i * ROW_H;

    tft.fillCircle(16, y + 13, 5, e.colour);

    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(COLOUR_DIM, TFT_BLACK);
    tft.drawString(timeAgo(e.when), tft.width() - MARGIN, y + 5, 2);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(fit(e.text, tft.width() - textX - MARGIN - agoW, 2), textX, y + 5, 2);
    tft.setTextColor(COLOUR_DIM, TFT_BLACK);
    tft.drawString(fit(e.repo, tft.width() - textX - MARGIN, 2), textX, y + 24, 2);
  }

  if (eventCount == 0 && !statusIsError) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COLOUR_DIM, TFT_BLACK);
    tft.drawString("No recent public activity", tft.width() / 2, tft.height() / 2, 2);
  }

  drawFooter();
}

// ---------- GitHub ----------

// Turns one event from the GitHub events API into a line of text and a colour.
void describe(JsonObjectConst ev, Event &out) {
  String type = ev["type"] | "";
  JsonObjectConst p = ev["payload"];
  String action = capitalised(ascii(p["action"] | ""));
  String ref = ascii(p["ref"] | "");
  if (ref.startsWith("refs/heads/")) ref = ref.substring(11);
  String refType = ascii(p["ref_type"] | "");

  if (type == "PushEvent") {
    out.text = "Pushed to " + ref;
    out.colour = TFT_GREEN;
  } else if (type == "PullRequestEvent") {
    int number = p["number"] | (p["pull_request"]["number"] | 0);
    out.text = action + " pull request #" + number;
    out.colour = TFT_MAGENTA;
  } else if (type == "PullRequestReviewEvent") {
    out.text = String("Reviewed pull request #") + (p["pull_request"]["number"] | 0);
    out.colour = TFT_MAGENTA;
  } else if (type == "PullRequestReviewCommentEvent") {
    out.text = String("Commented on pull request #") + (p["pull_request"]["number"] | 0);
    out.colour = TFT_CYAN;
  } else if (type == "IssuesEvent") {
    out.text = action + " issue #" + (p["issue"]["number"] | 0);
    out.colour = TFT_ORANGE;
  } else if (type == "IssueCommentEvent") {
    out.text = String("Commented on #") + (p["issue"]["number"] | 0);
    out.colour = TFT_CYAN;
  } else if (type == "CreateEvent") {
    out.text = "Created " + refType + (ref.length() ? " " + ref : "");
    out.colour = TFT_SKYBLUE;
  } else if (type == "DeleteEvent") {
    out.text = "Deleted " + refType + " " + ref;
    out.colour = TFT_RED;
  } else if (type == "WatchEvent") {
    out.text = "Starred";
    out.colour = TFT_YELLOW;
  } else if (type == "ForkEvent") {
    out.text = "Forked";
    out.colour = TFT_SKYBLUE;
  } else if (type == "ReleaseEvent") {
    out.text = "Released " + ascii(p["release"]["tag_name"] | "");
    out.colour = TFT_GOLD;
  } else {
    if (type.endsWith("Event")) type.remove(type.length() - 5);
    out.text = type;
    out.colour = TFT_LIGHTGREY;
  }

  out.repo = ascii(ev["repo"]["name"] | "");
  out.when = parseIsoUtc(ev["created_at"] | "");
}

// Fetches a GitHub API path into doc, keeping only the fields named in filter.
// Returns the HTTP status code, or a negative number on a connection or parse error.
int githubGet(const String &path, JsonDocument &doc, const JsonDocument &filter) {
  WiFiClientSecure client;
  // The server certificate is not verified. Only public, read-only data is
  // requested and no credentials are sent.
  client.setInsecure();

  HTTPClient http;
  http.useHTTP10(true);  // no chunked encoding, so the body can be parsed as a stream
  http.setTimeout(15000);
  if (!http.begin(client, "https://api.github.com" + path)) return -1;
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
  }
  http.end();
  return code;
}

String errorText(int code) {
  if (code == 403 || code == 429) return "GitHub rate limit reached, will retry";
  if (code == 404) return "GitHub user " GITHUB_USER " not found";
  if (code == -2) return "Could not read GitHub's reply";
  if (code < 0) return "Could not reach GitHub";
  return "GitHub returned HTTP " + String(code);
}

void refresh() {
  if (WiFi.status() != WL_CONNECTED) {
    setStatus("WiFi not connected, retrying", true);
    return;
  }
  setStatus("Updating...");

  JsonDocument filter;
  JsonDocument doc;

  filter["name"] = true;
  filter["public_repos"] = true;
  filter["followers"] = true;
  int code = githubGet("/users/" GITHUB_USER, doc, filter);
  if (code == HTTP_CODE_OK) {
    profileLine = ascii(doc["name"] | "");
    if (profileLine.length()) profileLine += " - ";
    profileLine += String(doc["public_repos"] | 0) + " repos, " +
                   String(doc["followers"] | 0) + " followers";
  }

  if (code == HTTP_CODE_OK) {
    filter.clear();
    doc.clear();
    JsonObject f = filter.add<JsonObject>();
    f["type"] = true;
    f["created_at"] = true;
    f["repo"]["name"] = true;
    f["payload"]["action"] = true;
    f["payload"]["ref"] = true;
    f["payload"]["ref_type"] = true;
    f["payload"]["number"] = true;
    f["payload"]["issue"]["number"] = true;
    f["payload"]["pull_request"]["number"] = true;
    f["payload"]["release"]["tag_name"] = true;
    code = githubGet("/users/" GITHUB_USER "/events/public?per_page=" + String(MAX_EVENTS),
                     doc, filter);
  }

  if (code != HTTP_CODE_OK) {
    // Keep showing the previous events; only the footer changes.
    setStatus(errorText(code), true);
    return;
  }

  eventCount = 0;
  for (JsonObjectConst ev : doc.as<JsonArrayConst>()) {
    if (eventCount >= MAX_EVENTS) break;
    describe(ev, events[eventCount++]);
  }
  // GitHub does not always return events strictly newest first.
  std::stable_sort(events, events + eventCount,
                   [](const Event &a, const Event &b) { return a.when > b.when; });

  statusIsError = false;
  statusLine = "Tap to refresh";
  struct tm now;
  if (clockIsSet() && getLocalTime(&now, 0)) {
    char buf[40];
    strftime(buf, sizeof(buf), "Updated %H:%M UTC - tap to refresh", &now);
    statusLine = buf;
  }
  Serial.printf("Loaded %d events\n", eventCount);
  drawScreen();
}

void connectWifi() {
  setStatus("Connecting to WiFi " WIFI_SSID "...");
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(250);
  }
  if (WiFi.status() == WL_CONNECTED) {
    setStatus("WiFi connected: " + WiFi.localIP().toString());
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    struct tm now;
    getLocalTime(&now, 5000);  // wait briefly for the clock so ages can be shown
  } else {
    setStatus("Could not join WiFi " WIFI_SSID, true);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\nGitHub activity display starting");

  tft.init();
  tft.setRotation(TFT_ROTATION);
  touchOk = touchBegin();
  drawScreen();

  connectWifi();
  refresh();
}

void loop() {
  static uint32_t lastRefresh = millis();
  static bool wasTouched = false;

  bool isTouched = touched();
  bool tapped = isTouched && !wasTouched && millis() - lastRefresh > 3000;
  wasTouched = isTouched;

  // Retry sooner after a failure than after a successful update.
  uint32_t interval = statusIsError ? 30000UL : REFRESH_MINUTES * 60000UL;
  if (tapped || millis() - lastRefresh > interval) {
    refresh();
    lastRefresh = millis();
  }
  delay(20);
}

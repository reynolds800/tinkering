// GitHub activity display: shows a user's recent public events; tap the screen
// to refresh. This file is shared by the device and the simulator, so it must
// not use anything Arduino- or ESP32-specific. See include/platform.h.

#include "app.h"

#include <ArduinoJson.h>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>

#include "display.h"
#include "platform.h"
#include "settings.h"

static const int MARGIN = 12;
static const int HEADER_H = 64;
static const int ROW_H = 50;
static const int MAX_EVENTS = 8;
static const int FOOTER_Y = 469;

static const uint32_t COLOUR_DIM = 0x808080u;
static const uint32_t COLOUR_RULE = 0x404040u;

static const lgfx::IFont *const FONT_SMALL = &fonts::Font0;
static const lgfx::IFont *const FONT_BODY = &fonts::Font2;
static const lgfx::IFont *const FONT_TITLE = &fonts::Font4;

struct Event {
  std::string text;
  std::string repo;
  time_t when;
  uint16_t colour;  // RGB565, as the TFT_ colour constants are
};

Display lcd;

static Event events[MAX_EVENTS];
static int eventCount = 0;
static std::string profileLine;
static std::string statusLine = "Starting";
static bool statusIsError = false;

// ---------- text helpers ----------

// The built-in fonts only cover ASCII.
static std::string ascii(const char *s) {
  std::string out;
  if (!s) return out;
  for (; *s; s++) {
    unsigned char c = (unsigned char)*s;
    if (c >= 32 && c < 127) {
      out += (char)c;
    } else if ((c & 0xC0) != 0x80) {  // one '?' per multi-byte character
      out += '?';
    }
  }
  return out;
}

static std::string fit(std::string s, int maxWidth, const lgfx::IFont *font) {
  if (lcd.textWidth(s.c_str(), font) <= maxWidth) return s;
  while (s.size() > 1 && lcd.textWidth((s + "..").c_str(), font) > maxWidth) {
    s.pop_back();
  }
  return s + "..";
}

static std::string capitalised(std::string s) {
  if (!s.empty()) s[0] = (char)toupper((unsigned char)s[0]);
  return s;
}

// Converts "2026-11-14T10:56:00Z" to seconds since 1970, or 0 if malformed.
static time_t parseIsoUtc(const char *iso) {
  int y, mo, d, h, mi, s;
  if (!iso || sscanf(iso, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6) return 0;
  // Days since 1970-01-01 for a proleptic Gregorian date.
  y -= mo <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  long yoe = y - era * 400;
  long doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = era * 146097 + doe - 719468;
  return (time_t)days * 86400 + h * 3600 + mi * 60 + s;
}

static bool clockIsSet() { return time(nullptr) > 1600000000; }

static std::string timeAgo(time_t when) {
  if (!when || !clockIsSet()) return "";
  long secs = (long)(time(nullptr) - when);
  if (secs < 60) return "now";
  if (secs < 3600) return std::to_string(secs / 60) + "m";
  if (secs < 86400) return std::to_string(secs / 3600) + "h";
  return std::to_string(secs / 86400) + "d";
}

// ---------- drawing ----------

static void drawFooter() {
  lcd.fillRect(0, FOOTER_Y - 2, lcd.width(), lcd.height() - FOOTER_Y + 2, TFT_BLACK);
  lcd.setTextDatum(top_left);
  lcd.setTextColor(statusIsError ? 0xFF4040u : COLOUR_DIM, TFT_BLACK);
  lcd.drawString(fit(statusLine, lcd.width() - 2 * MARGIN, FONT_SMALL).c_str(), MARGIN, FOOTER_Y,
                 FONT_SMALL);
}

static void setStatus(const std::string &message, bool isError = false) {
  statusLine = message;
  statusIsError = isError;
  printf("%s\n", message.c_str());
  drawFooter();
}

static void drawScreen() {
  lcd.startWrite();
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextDatum(top_left);

  int fullWidth = lcd.width() - 2 * MARGIN;
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.drawString(fit("@" GITHUB_USER, fullWidth, FONT_TITLE).c_str(), MARGIN, 8, FONT_TITLE);
  lcd.setTextColor(COLOUR_DIM, TFT_BLACK);
  lcd.drawString(fit(profileLine.empty() ? "GitHub activity" : profileLine, fullWidth, FONT_BODY)
                     .c_str(),
                 MARGIN, 38, FONT_BODY);
  lcd.drawFastHLine(0, HEADER_H - 3, lcd.width(), COLOUR_RULE);

  const int textX = 30;
  const int agoW = 36;
  for (int i = 0; i < eventCount; i++) {
    const Event &e = events[i];
    int y = HEADER_H + i * ROW_H;

    lcd.fillCircle(16, y + 13, 5, e.colour);

    lcd.setTextDatum(top_right);
    lcd.setTextColor(COLOUR_DIM, TFT_BLACK);
    lcd.drawString(timeAgo(e.when).c_str(), lcd.width() - MARGIN, y + 5, FONT_BODY);

    lcd.setTextDatum(top_left);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.drawString(fit(e.text, lcd.width() - textX - MARGIN - agoW, FONT_BODY).c_str(), textX,
                   y + 5, FONT_BODY);
    lcd.setTextColor(COLOUR_DIM, TFT_BLACK);
    lcd.drawString(fit(e.repo, lcd.width() - textX - MARGIN, FONT_BODY).c_str(), textX, y + 24,
                   FONT_BODY);
  }

  if (eventCount == 0 && !statusIsError) {
    lcd.setTextDatum(middle_center);
    lcd.setTextColor(COLOUR_DIM, TFT_BLACK);
    lcd.drawString("No recent public activity", lcd.width() / 2, lcd.height() / 2, FONT_BODY);
  }

  drawFooter();
  lcd.endWrite();
}

// ---------- GitHub ----------

// Turns one event from the GitHub events API into a line of text and a colour.
static void describe(JsonObjectConst ev, Event &out) {
  std::string type = ev["type"] | "";
  JsonObjectConst p = ev["payload"];
  std::string action = capitalised(ascii(p["action"] | ""));
  std::string ref = ascii(p["ref"] | "");
  if (ref.rfind("refs/heads/", 0) == 0) ref = ref.substr(11);
  std::string refType = ascii(p["ref_type"] | "");
  std::string prNumber = std::to_string(p["number"] | (p["pull_request"]["number"] | 0));
  std::string issueNumber = std::to_string(p["issue"]["number"] | 0);

  if (type == "PushEvent") {
    out.text = "Pushed to " + ref;
    out.colour = TFT_GREEN;
  } else if (type == "PullRequestEvent") {
    out.text = action + " pull request #" + prNumber;
    out.colour = TFT_MAGENTA;
  } else if (type == "PullRequestReviewEvent") {
    out.text = "Reviewed pull request #" + prNumber;
    out.colour = TFT_MAGENTA;
  } else if (type == "PullRequestReviewCommentEvent") {
    out.text = "Commented on pull request #" + prNumber;
    out.colour = TFT_CYAN;
  } else if (type == "IssuesEvent") {
    out.text = action + " issue #" + issueNumber;
    out.colour = TFT_ORANGE;
  } else if (type == "IssueCommentEvent") {
    out.text = "Commented on #" + issueNumber;
    out.colour = TFT_CYAN;
  } else if (type == "CreateEvent") {
    out.text = "Created " + refType + (ref.empty() ? "" : " " + ref);
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
    if (type.size() > 5 && type.compare(type.size() - 5, 5, "Event") == 0) {
      type.erase(type.size() - 5);
    }
    out.text = type;
    out.colour = TFT_LIGHTGREY;
  }

  out.repo = ascii(ev["repo"]["name"] | "");
  out.when = parseIsoUtc(ev["created_at"] | "");
}

static std::string errorText(int code) {
  if (code == 403 || code == 429) return "GitHub rate limit reached, will retry";
  if (code == 404) return "GitHub user " GITHUB_USER " not found";
  if (code == -2) return "Could not read GitHub's reply";
  if (code < 0) return "Could not reach GitHub";
  return "GitHub returned HTTP " + std::to_string(code);
}

static void refresh() {
  if (!platformOnline()) {
    setStatus("Not connected to " + platformNetworkName() + ", retrying", true);
    return;
  }
  setStatus("Updating...");

  JsonDocument filter;
  JsonDocument doc;

  filter["name"] = true;
  filter["public_repos"] = true;
  filter["followers"] = true;
  int code = platformGithubGet("/users/" GITHUB_USER, doc, filter);
  if (code == 200) {
    profileLine = ascii(doc["name"] | "");
    if (!profileLine.empty()) profileLine += " - ";
    profileLine += std::to_string(doc["public_repos"] | 0) + " repos, " +
                   std::to_string(doc["followers"] | 0) + " followers";

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
    code = platformGithubGet(
        "/users/" GITHUB_USER "/events/public?per_page=" + std::to_string(MAX_EVENTS), doc, filter);
  }

  if (code != 200) {
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
  if (clockIsSet()) {
    time_t now = time(nullptr);
    char buf[40];
    strftime(buf, sizeof(buf), "Updated %H:%M UTC - tap to refresh", gmtime(&now));
    statusLine = buf;
  }
  printf("Loaded %d events\n", eventCount);
  drawScreen();
}

// ---------- entry points ----------

static uint32_t lastRefresh = 0;

void appSetup() {
  printf("GitHub activity display starting\n");

  lcd.init();
  lcd.setRotation(SCREEN_ROTATION);
  lcd.setBrightness(200);
  drawScreen();

  setStatus("Connecting to " + platformNetworkName() + "...");
  if (!platformConnect()) {
    setStatus("Could not connect to " + platformNetworkName(), true);
  }
  refresh();
  lastRefresh = lgfx::millis();
}

void appLoop() {
  static bool wasTouched = false;

  int32_t x, y;
  bool isTouched = lcd.getTouch(&x, &y) > 0;
  bool tapped = isTouched && !wasTouched && lgfx::millis() - lastRefresh > 3000;
  wasTouched = isTouched;

  // Retry sooner after a failure than after a successful update.
  uint32_t interval = statusIsError ? 30000UL : REFRESH_MINUTES * 60000UL;
  if (tapped || lgfx::millis() - lastRefresh > interval) {
    refresh();
    lastRefresh = lgfx::millis();
  }
  lgfx::delay(20);
}

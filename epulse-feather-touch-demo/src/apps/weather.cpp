// Weather: live conditions, an hour-by-hour forecast for the next 24 hours
// and a three-day outlook for a place, which can be changed by searching.
// Weather and place search come from Open-Meteo (open-meteo.com), which
// needs no account or key.

#include <ArduinoJson.h>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "apps.h"
#include "keyboard.h"
#include "platform.h"

// ---------- layout ----------

static const int MARGIN = 12;
static const int SEARCH_TOP = TITLE_H + 4;
static const int SEARCH_H = 34;
static const int NOW_TOP = SEARCH_TOP + SEARCH_H + 2;
static const int HOURLY_TOP = NOW_TOP + 114;
static const int HOURLY_H = 106;
static const int DAILY_TOP = HOURLY_TOP + HOURLY_H + 4;
static const int DAY_ROW_H = 44;
static const int FOOTER_Y = SCREEN_H - 14;

static const lgfx::IFont *const FONT_BODY = &fonts::DejaVu12;
static const lgfx::IFont *const FONT_SMALL = &fonts::DejaVu9;

static const uint32_t COLOUR_RAIN = 0x4DA3FFu;
static const uint32_t COLOUR_SUN = 0xFFD60Au;
static const uint32_t COLOUR_MOON = 0xF4E9B0u;

// ---------- state ----------

struct Place {
  std::string name;
  double latitude, longitude;
};

struct Hour {
  int hour;  // 0 to 23, local to the place
  int temperature;
  int code;  // WMO weather code
  int rainChance;
  int wind;  // km/h
  bool daylight;
};

struct Day {
  int weekday;  // 0 is Sunday
  int code;
  int high, low;
  int rainChance;
  int wind;
};

static const int HOURS = 24;
static const int HOURS_PER_PAGE = 6;
static const int DAYS = 3;
static const size_t MAX_PLACES = 5;
static const int WINDY_KMH = 35;  // above this, fair weather is shown as windy
static const uint32_t REFRESH_MS = 15 * 60000UL;

static std::vector<Place> places;  // most recently chosen first; places[0] is on show

static bool loaded = false;
static Hour now;  // current conditions; `hour` is unused
static int feelsLike = 0;
static Hour hours[HOURS];
static int hourCount = 0;
static int hourPage = 0;
static Day days[DAYS];
static int dayCount = 0;

static std::string statusLine;
static bool statusIsError = false;
static bool refreshWanted = false;
static uint32_t lastAttempt = 0;

// The place search screen.
static bool searching = false;
static std::string query;
static std::string searchHint;
static std::vector<Place> choices;

static const Place &place() { return places[0]; }

// ---------- weather symbols ----------

enum Symbol { SUNNY, CLEAR_NIGHT, PARTLY_CLOUDY, PARTLY_CLOUDY_NIGHT, CLOUDY, RAINY, THUNDERSTORM,
              WINDY, FOGGY, SNOWY };

static bool isRain(int code) { return (code >= 51 && code <= 67) || (code >= 80 && code <= 82); }
static bool isSnow(int code) { return (code >= 71 && code <= 77) || code == 85 || code == 86; }

// Chooses a symbol from the WMO weather code, the wind and whether it is day.
static Symbol symbolFor(int code, int wind, bool daylight) {
  if (code >= 95) return THUNDERSTORM;
  if (isSnow(code)) return SNOWY;
  if (isRain(code)) return RAINY;
  if (code == 45 || code == 48) return FOGGY;
  if (wind >= WINDY_KMH) return WINDY;
  if (code == 3) return CLOUDY;
  if (code == 2) return daylight ? PARTLY_CLOUDY : PARTLY_CLOUDY_NIGHT;
  return daylight ? SUNNY : CLEAR_NIGHT;
}

static const char *describe(int code, int wind, bool daylight) {
  switch (symbolFor(code, wind, daylight)) {
    case THUNDERSTORM: return "Thunderstorm";
    case SNOWY: return "Snow";
    case RAINY: return code <= 57 ? "Drizzle" : code >= 80 ? "Showers" : "Rain";
    case FOGGY: return "Foggy";
    case WINDY: return "Windy";
    case CLOUDY: return "Cloudy";
    case PARTLY_CLOUDY:
    case PARTLY_CLOUDY_NIGHT: return "Partly cloudy";
    case SUNNY: return "Sunny";
    case CLEAR_NIGHT: return "Clear";
  }
  return "";
}

// The symbols are drawn from shapes, on a grid 64 units across that is
// scaled to the size asked for. cx, cy is the centre.
static void drawCloud(float cx, float cy, float u, uint32_t fill) {
  lcd.fillSmoothCircle((int)(cx - 10 * u), (int)(cy + 2 * u), (int)(9 * u), fill);
  lcd.fillSmoothCircle((int)(cx + 1 * u), (int)(cy - 5 * u), (int)(12 * u), fill);
  lcd.fillSmoothCircle((int)(cx + 13 * u), (int)(cy + 3 * u), (int)(8 * u), fill);
  lcd.fillSmoothRoundRect((int)(cx - 19 * u), (int)(cy + 1 * u), (int)(40 * u), (int)(10 * u + 1),
                          (int)(5 * u), fill);
}

static void drawSun(float cx, float cy, float u, float r) {
  lcd.fillSmoothCircle((int)cx, (int)cy, (int)r, COLOUR_SUN);
  for (int i = 0; i < 8; i++) {
    float angle = i * 3.14159f / 4;
    float dx = cosf(angle), dy = sinf(angle);
    lcd.drawWideLine((int)(cx + dx * (r + 4 * u)), (int)(cy + dy * (r + 4 * u)),
                     (int)(cx + dx * (r + 8 * u)), (int)(cy + dy * (r + 8 * u)),
                     u < 0.6f ? 0.8f : 1.5f * u, COLOUR_SUN);
  }
}

static void drawMoon(float cx, float cy, float r) {
  lcd.fillSmoothCircle((int)cx, (int)cy, (int)r, COLOUR_MOON);
  lcd.fillSmoothCircle((int)(cx + r * 0.5f), (int)(cy - r * 0.35f), (int)(r * 0.85f),
                       colour::background);
}

static void drawSymbol(Symbol symbol, int centreX, int centreY, int size) {
  float u = size / 64.0f;
  float cx = centreX, cy = centreY;
  const uint32_t white = 0xE8ECF0u, grey = 0x9AA5B1u, dark = 0x6B7684u;
  float stroke = u < 0.6f ? 0.8f : 1.6f * u;

  switch (symbol) {
    case SUNNY:
      drawSun(cx, cy, u, 13 * u);
      break;
    case CLEAR_NIGHT:
      drawMoon(cx, cy, 16 * u);
      break;
    case PARTLY_CLOUDY:
      drawSun(cx + 9 * u, cy - 9 * u, u, 10 * u);
      drawCloud(cx - 3 * u, cy + 6 * u, u, white);
      break;
    case PARTLY_CLOUDY_NIGHT:
      drawMoon(cx + 10 * u, cy - 10 * u, 12 * u);
      drawCloud(cx - 3 * u, cy + 6 * u, u, white);
      break;
    case CLOUDY:
      drawCloud(cx + 7 * u, cy - 8 * u, u * 0.8f, dark);
      drawCloud(cx - 2 * u, cy + 3 * u, u, white);
      break;
    case RAINY:
      drawCloud(cx, cy - 8 * u, u, grey);
      for (int i = 0; i < 3; i++) {
        float x = cx - 9 * u + i * 10 * u;
        lcd.drawWideLine((int)x, (int)(cy + 10 * u), (int)(x - 4 * u), (int)(cy + 20 * u), stroke,
                         COLOUR_RAIN);
      }
      break;
    case THUNDERSTORM:
      drawCloud(cx, cy - 8 * u, u, dark);
      lcd.fillTriangle((int)(cx + 5 * u), (int)(cy + 4 * u), (int)(cx - 6 * u), (int)(cy + 16 * u),
                       (int)(cx + 3 * u), (int)(cy + 14 * u), COLOUR_SUN);
      lcd.fillTriangle((int)(cx - 3 * u), (int)(cy + 27 * u), (int)(cx + 8 * u), (int)(cy + 12 * u),
                       (int)(cx - 2 * u), (int)(cy + 14 * u), COLOUR_SUN);
      break;
    case WINDY: {
      // Three gusts, the upper two curling over at the end.
      const float ends[3] = {2, 15, 6};
      for (int i = 0; i < 3; i++) {
        float y = cy + (i - 1) * 11 * u;
        float end = cx + ends[i] * u;
        lcd.drawWideLine((int)(cx - 20 * u), (int)y, (int)end, (int)y, stroke, white);
        if (i == 2) continue;
        // The curl is most of a circle sitting on the end of the line,
        // drawn as short straight pieces.
        float r = 5 * u, fromX = end, fromY = y;
        for (int step = 1; step <= 10; step++) {
          float angle = (90 - step * 26) * 3.14159f / 180;
          float toX = end + r * cosf(angle), toY = y - r + r * sinf(angle);
          lcd.drawWideLine((int)lroundf(fromX), (int)lroundf(fromY), (int)lroundf(toX),
                           (int)lroundf(toY), stroke, white);
          fromX = toX;
          fromY = toY;
        }
      }
      break;
    }
    case FOGGY:
      drawCloud(cx, cy - 9 * u, u, grey);
      for (int i = 0; i < 3; i++) {
        float y = cy + 9 * u + i * 6 * u;
        lcd.drawWideLine((int)(cx - 16 * u + i * 3 * u), (int)y, (int)(cx + 16 * u - i * 3 * u),
                         (int)y, stroke, white);
      }
      break;
    case SNOWY:
      drawCloud(cx, cy - 8 * u, u, grey);
      for (int i = 0; i < 3; i++) {
        lcd.fillSmoothCircle((int)(cx - 10 * u + i * 10 * u), (int)(cy + (i == 1 ? 20 : 14) * u),
                             (int)(2.5f * u + 0.5f), 0xFFFFFFu);
      }
      break;
  }
}

// ---------- text helpers ----------

static std::string fit(std::string s, int maxWidth, const lgfx::IFont *font) {
  if (lcd.textWidth(s.c_str(), font) <= maxWidth) return s;
  while (s.size() > 1 && lcd.textWidth((s + "..").c_str(), font) > maxWidth) s.pop_back();
  return s + "..";
}

// Makes text safe to draw and to save: the fonts only cover ASCII, and '|'
// and ';' separate saved places.
static std::string plain(const char *s) {
  std::string out;
  for (; s && *s; s++) {
    unsigned char c = (unsigned char)*s;
    if (c >= 32 && c < 127 && c != '|' && c != ';') {
      out += (char)c;
    } else if (c >= 127 && (c & 0xC0) != 0x80) {  // one '?' per accented character
      out += '?';
    }
  }
  return out;
}

// Draws a temperature with a degree sign, which the fonts lack. x is the
// left edge, or the right edge if rightAligned. Returns the width used.
static int drawDegrees(int value, int x, int y, const lgfx::IFont *font, uint32_t ink,
                       bool rightAligned = false) {
  std::string text = std::to_string(value);
  int textW = lcd.textWidth(text.c_str(), font);
  int height = lcd.fontHeight(font);
  int r = height >= 40 ? 5 : height >= 18 ? 3 : 2;
  int total = textW + 2 * r + 3;
  if (rightAligned) x -= total;
  lcd.setTextDatum(top_left);
  lcd.setTextColor(ink);
  lcd.drawString(text.c_str(), x, y, font);
  int ringX = x + textW + r + 2, ringY = y + r + (height >= 40 ? 6 : 1);
  lcd.drawCircle(ringX, ringY, r, ink);
  if (r > 2) lcd.drawCircle(ringX, ringY, r - 1, ink);
  return total;
}

static std::string hourLabel(int hour) {
  if (hour == 0) return "12am";
  if (hour == 12) return "12pm";
  return std::to_string(hour % 12) + (hour < 12 ? "am" : "pm");
}

// ---------- drawing: forecast ----------

static void drawFooter() {
  lcd.fillRect(0, FOOTER_Y - 3, lcd.width(), lcd.height() - FOOTER_Y + 3, colour::background);
  lcd.setTextDatum(top_left);
  lcd.setTextColor(statusIsError ? colour::red : colour::dim);
  lcd.drawString(fit(statusLine, lcd.width() - 2 * MARGIN, FONT_SMALL).c_str(), MARGIN, FOOTER_Y,
                 FONT_SMALL);
}

static void setStatus(const std::string &message, bool isError = false) {
  statusLine = message;
  statusIsError = isError;
  printf("Weather: %s\n", message.c_str());
  drawFooter();
}

static void drawMagnifier(int cx, int cy, uint32_t ink) {
  lcd.fillSmoothCircle(cx - 2, cy - 2, 6, ink);
  lcd.fillSmoothCircle(cx - 2, cy - 2, 4, colour::bar);
  lcd.drawWideLine(cx + 3, cy + 3, cx + 7, cy + 7, 1.2f, ink);
}

// The place on show, drawn as a search bar: tapping it opens the search.
static void drawSearchBar() {
  lcd.fillSmoothRoundRect(6, SEARCH_TOP, lcd.width() - 12, SEARCH_H - 4, 15, colour::bar);
  drawMagnifier(24, SEARCH_TOP + 15, colour::dim);
  lcd.setTextDatum(middle_left);
  lcd.setTextColor(colour::text);
  lcd.drawString(fit(place().name, lcd.width() - 60, FONT_BODY).c_str(), 40, SEARCH_TOP + 15,
                 FONT_BODY);
}

static void drawNow() {
  drawSymbol(symbolFor(now.code, now.wind, now.daylight), 60, NOW_TOP + 50, 96);

  const int x = 124;
  drawDegrees(now.temperature, x, NOW_TOP + 4, &fonts::DejaVu56, colour::text);
  lcd.setTextDatum(top_left);
  lcd.setTextColor(colour::text);
  lcd.drawString(describe(now.code, now.wind, now.daylight), x, NOW_TOP + 62, &fonts::DejaVu18);

  lcd.setTextColor(colour::dim);
  int at = x;
  lcd.drawString("Feels", at, NOW_TOP + 88, FONT_BODY);
  at += lcd.textWidth("Feels ", FONT_BODY);
  at += drawDegrees(feelsLike, at, NOW_TOP + 88, FONT_BODY, colour::dim) + 8;
  lcd.drawString(("Wind " + std::to_string(now.wind) + " km/h").c_str(), at, NOW_TOP + 88, FONT_BODY);
}

// Six hours at a time; tapping the strip moves on to the next six.
static void drawHourly() {
  lcd.fillRect(0, HOURLY_TOP, lcd.width(), HOURLY_H, colour::background);
  lcd.drawFastHLine(0, HOURLY_TOP, lcd.width(), colour::rule);
  lcd.drawFastHLine(0, HOURLY_TOP + HOURLY_H - 1, lcd.width(), colour::rule);

  int pages = (hourCount + HOURS_PER_PAGE - 1) / HOURS_PER_PAGE;
  lcd.setTextDatum(top_left);
  lcd.setTextColor(colour::dim);
  lcd.drawString("Next 24 hours - tap for later", MARGIN, HOURLY_TOP + 6, FONT_SMALL);
  for (int i = 0; i < pages; i++) {  // dots showing which page is on show
    lcd.fillSmoothCircle(lcd.width() - MARGIN - (pages - 1 - i) * 10 - 3, HOURLY_TOP + 10, 2,
                         i == hourPage ? colour::text : colour::rule);
  }

  const int columnW = lcd.width() / HOURS_PER_PAGE;
  for (int i = 0; i < HOURS_PER_PAGE; i++) {
    int index = hourPage * HOURS_PER_PAGE + i;
    if (index >= hourCount) break;
    const Hour &h = hours[index];
    int cx = i * columnW + columnW / 2 + 1;

    lcd.setTextDatum(top_center);
    lcd.setTextColor(index == 0 ? colour::text : colour::dim);
    lcd.drawString(index == 0 ? "Now" : hourLabel(h.hour).c_str(), cx, HOURLY_TOP + 22, FONT_SMALL);
    drawSymbol(symbolFor(h.code, h.wind, h.daylight), cx, HOURLY_TOP + 50, 34);

    std::string temperature = std::to_string(h.temperature);
    int w = lcd.textWidth(temperature.c_str(), FONT_BODY) + 7;
    drawDegrees(h.temperature, cx - w / 2, HOURLY_TOP + 70, FONT_BODY, colour::text);

    lcd.setTextDatum(top_center);
    lcd.setTextColor(h.rainChance >= 30 ? COLOUR_RAIN : colour::rule);
    lcd.drawString((std::to_string(h.rainChance) + "%").c_str(), cx, HOURLY_TOP + 89, FONT_SMALL);
  }
}

static void drawDaily() {
  static const char *const WEEKDAYS[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  for (int i = 0; i < dayCount; i++) {
    const Day &d = days[i];
    int y = DAILY_TOP + i * DAY_ROW_H;
    int cy = y + DAY_ROW_H / 2;

    lcd.setTextDatum(middle_left);
    lcd.setTextColor(colour::text);
    lcd.drawString(WEEKDAYS[d.weekday], MARGIN, cy, &fonts::DejaVu18);
    drawSymbol(symbolFor(d.code, d.wind, true), 84, cy, 40);

    lcd.setTextDatum(top_left);
    lcd.setTextColor(colour::text);
    lcd.drawString(describe(d.code, d.wind, true), 112, y + 7, FONT_BODY);
    lcd.setTextColor(d.rainChance >= 30 ? COLOUR_RAIN : colour::dim);
    lcd.drawString((std::to_string(d.rainChance) + "% rain").c_str(), 112, y + 25, FONT_SMALL);

    // High in white, low in grey.
    drawDegrees(d.low, lcd.width() - MARGIN, y + 13, &fonts::DejaVu18, colour::dim, true);
    drawDegrees(d.high, lcd.width() - MARGIN - 50, y + 13, &fonts::DejaVu18, colour::text, true);
    if (i < dayCount - 1) {
      lcd.drawFastHLine(MARGIN, y + DAY_ROW_H - 1, lcd.width() - 2 * MARGIN, colour::bar);
    }
  }
}

static void drawForecast() {
  lcd.startWrite();
  lcd.fillRect(0, TITLE_H, lcd.width(), lcd.height() - TITLE_H, colour::background);
  drawSearchBar();
  if (loaded) {
    drawNow();
    drawHourly();
    drawDaily();
  } else {
    lcd.setTextDatum(middle_center);
    lcd.setTextColor(colour::dim);
    lcd.drawString(statusIsError ? "Nothing to show" : "Loading...", lcd.width() / 2,
                   lcd.height() / 2, &fonts::DejaVu18);
  }
  drawFooter();
  lcd.endWrite();
}

// ---------- loading ----------

static int weekdayOf(const char *date) {  // "2026-11-14" to 0 (Sunday) to 6
  int y, m, d;
  if (!date || sscanf(date, "%d-%d-%d", &y, &m, &d) != 3) return 0;
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  long yoe = y - era * 400;
  long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long day = era * 146097 + doe - 719468;
  return (int)(((day + 4) % 7 + 7) % 7);
}

static int rounded(JsonVariantConst value) { return (int)lround(value | 0.0); }

static void refresh() {
  refreshWanted = false;
  lastAttempt = lgfx::millis();
  if (!platformOnline()) {
    setStatus("Not connected to " + platformNetworkName() + ", retrying", true);
    return;
  }
  setStatus("Updating...");

  char url[640];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,apparent_temperature,weather_code,wind_speed_10m,is_day"
           "&hourly=temperature_2m,weather_code,precipitation_probability,wind_speed_10m,is_day"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
           "precipitation_probability_max,wind_speed_10m_max"
           "&timezone=auto&forecast_days=4&forecast_hours=%d",
           place().latitude, place().longitude, HOURS);

  JsonDocument filter, doc;
  filter["current"] = true;
  filter["hourly"] = true;
  filter["daily"] = true;
  int code = platformJsonGet(url, doc, filter);
  JsonObjectConst current = doc["current"], hourly = doc["hourly"], daily = doc["daily"];
  if (code != 200 || current.isNull() || hourly.isNull() || daily.isNull()) {
    std::string problem = code == 429  ? "Weather service is limiting requests, will retry"
                          : code == -2 ? "Could not read the weather service's reply"
                          : code < 0   ? "Could not reach the weather service"
                          : code != 200 ? "Weather service returned HTTP " + std::to_string(code)
                                        : "The weather service sent no forecast";
    // Keep showing what was loaded before; only the footer changes.
    setStatus(problem, true);
    if (!loaded) drawForecast();
    return;
  }

  now.temperature = rounded(current["temperature_2m"]);
  now.code = current["weather_code"] | 0;
  now.wind = rounded(current["wind_speed_10m"]);
  now.daylight = (current["is_day"] | 1) != 0;
  feelsLike = rounded(current["apparent_temperature"]);

  hourCount = 0;
  JsonArrayConst times = hourly["time"];
  for (size_t i = 0; i < times.size() && hourCount < HOURS; i++) {
    const char *time = times[i] | "";  // "2026-11-14T15:00"
    Hour &h = hours[hourCount++];
    h.hour = strlen(time) >= 13 ? atoi(time + 11) : 0;
    h.temperature = rounded(hourly["temperature_2m"][i]);
    h.code = hourly["weather_code"][i] | 0;
    h.rainChance = hourly["precipitation_probability"][i] | 0;
    h.wind = rounded(hourly["wind_speed_10m"][i]);
    h.daylight = (hourly["is_day"][i] | 1) != 0;
  }

  // The first day in the list is today, which the hourly strip covers.
  dayCount = 0;
  JsonArrayConst dates = daily["time"];
  for (size_t i = 1; i < dates.size() && dayCount < DAYS; i++) {
    Day &d = days[dayCount++];
    d.weekday = weekdayOf(dates[i] | "");
    d.code = daily["weather_code"][i] | 0;
    d.high = rounded(daily["temperature_2m_max"][i]);
    d.low = rounded(daily["temperature_2m_min"][i]);
    d.rainChance = daily["precipitation_probability_max"][i] | 0;
    d.wind = rounded(daily["wind_speed_10m_max"][i]);
  }

  loaded = true;
  hourPage = 0;
  statusIsError = false;
  statusLine = time(nullptr) > 1600000000 ? "Updated " + clockText() + " - data by Open-Meteo.com"
                                          : "Weather data by Open-Meteo.com";
  printf("Weather: loaded %s: %d C, code %d, %d hours, %d days\n", place().name.c_str(),
         now.temperature, now.code, hourCount, dayCount);
  drawForecast();
}

// ---------- places ----------

static void savePlaces() {
  std::string saved;
  char buf[48];
  for (auto &p : places) {
    snprintf(buf, sizeof(buf), "|%.4f|%.4f", p.latitude, p.longitude);
    saved += (saved.empty() ? "" : ";") + p.name + buf;
  }
  platformSaveSetting("weather_places", saved);
}

static void loadPlaces() {
  if (!places.empty()) return;
  std::string saved = platformLoadSetting("weather_places");
  size_t at = 0;
  while (at < saved.size() && places.size() < MAX_PLACES) {
    size_t end = saved.find(';', at);
    if (end == std::string::npos) end = saved.size();
    std::string entry = saved.substr(at, end - at);
    size_t bar1 = entry.find('|'), bar2 = entry.rfind('|');
    if (bar1 != std::string::npos && bar2 > bar1) {
      places.push_back({entry.substr(0, bar1), atof(entry.c_str() + bar1 + 1),
                        atof(entry.c_str() + bar2 + 1)});
    }
    at = end + 1;
  }
  if (places.empty()) places.push_back({"Terrigal, New South Wales, AU", -33.4482, 151.4467});
}

// ---------- searching for a place ----------

static const int CHOICE_TOP = TITLE_H + 34;
static const int CHOICE_H = 36;
static const int INPUT_H = 40;
static const int INPUT_TOP = keyboard::TOP - INPUT_H;

static void drawInput() {
  lcd.startWrite();
  lcd.fillRect(0, INPUT_TOP, lcd.width(), INPUT_H, colour::background);
  lcd.fillSmoothRoundRect(6, INPUT_TOP + 4, lcd.width() - 12, INPUT_H - 8, 10, colour::bar);
  int cy = INPUT_TOP + INPUT_H / 2;
  drawMagnifier(24, cy, colour::dim);
  int x = 40;
  lcd.setTextDatum(middle_left);
  if (query.empty()) {
    lcd.setTextColor(colour::dim);
    lcd.drawString("Town or city", x + 4, cy, &fonts::DejaVu18);
  } else {
    // Show the end of the text when it is too long for the box.
    std::string shown = query;
    while (lcd.textWidth(shown.c_str(), &fonts::DejaVu18) > lcd.width() - 70) shown.erase(0, 1);
    lcd.setTextColor(colour::text);
    lcd.drawString(shown.c_str(), x, cy, &fonts::DejaVu18);
    x += lcd.textWidth(shown.c_str(), &fonts::DejaVu18) + 2;
  }
  lcd.fillRect(x, cy - 10, 2, 20, colour::green);
  lcd.endWrite();
}

static void drawChoices() {
  lcd.startWrite();
  lcd.fillRect(0, TITLE_H, lcd.width(), INPUT_TOP - TITLE_H, colour::background);
  lcd.setTextDatum(top_left);
  lcd.setTextColor(colour::dim);
  lcd.drawString(fit(searchHint, lcd.width() - 2 * MARGIN, FONT_BODY).c_str(), MARGIN, TITLE_H + 12,
                 FONT_BODY);
  for (size_t i = 0; i < choices.size(); i++) {
    int y = CHOICE_TOP + (int)i * CHOICE_H;
    lcd.fillSmoothRoundRect(6, y + 2, lcd.width() - 12, CHOICE_H - 4, 10, colour::bar);
    lcd.setTextDatum(middle_left);
    lcd.setTextColor(colour::text);
    lcd.drawString(fit(choices[i].name, lcd.width() - 36, FONT_BODY).c_str(), 18, y + CHOICE_H / 2,
                   FONT_BODY);
  }
  lcd.endWrite();
}

static void openSearch() {
  searching = true;
  query.clear();
  choices = places;
  searchHint = "Type a place, or pick a recent one";
  drawChoices();
  drawInput();
  keyboard::open("Search", true);
}

// Looks the typed name up and lists the places found.
static void search() {
  // Web addresses may only contain certain characters; the rest are encoded.
  std::string encoded;
  char buf[4];
  for (unsigned char c : query) {
    if (isalnum(c)) {
      encoded += (char)c;
    } else {
      snprintf(buf, sizeof(buf), "%%%02X", c);
      encoded += buf;
    }
  }

  searchHint = "Searching...";
  choices.clear();
  drawChoices();
  printf("Weather: searching for %s\n", query.c_str());

  JsonDocument filter, doc;
  JsonObject f = filter["results"].add<JsonObject>();
  f["name"] = true;
  f["admin1"] = true;
  f["country_code"] = true;
  f["latitude"] = true;
  f["longitude"] = true;
  int code = platformJsonGet("https://geocoding-api.open-meteo.com/v1/search?name=" + encoded +
                                 "&count=5&language=en&format=json",
                             doc, filter);
  if (code != 200) {
    searchHint = platformOnline() ? "Could not reach the place search" : "Not connected";
  } else {
    for (JsonObjectConst result : doc["results"].as<JsonArrayConst>()) {
      std::string name = plain(result["name"] | "");
      std::string region = plain(result["admin1"] | "");
      std::string country = plain(result["country_code"] | "");
      if (name.empty()) continue;
      if (!region.empty() && region != name) name += ", " + region;
      if (!country.empty()) name += ", " + country;
      choices.push_back({name, result["latitude"] | 0.0, result["longitude"] | 0.0});
    }
    searchHint = choices.empty() ? "No places found for \"" + query + "\""
                                 : "Tap a place to see its weather";
  }
  printf("Weather: %d places found\n", (int)choices.size());
  drawChoices();
}

// Makes a place the one on show, remembers it, and loads its weather.
static void choose(const Place &chosen) {
  Place p = chosen;  // copied, as it may be an entry in the list being changed
  searching = false;
  if (p.name != place().name) {
    for (size_t i = 0; i < places.size(); i++) {
      if (places[i].name == p.name) {
        places.erase(places.begin() + i);
        break;
      }
    }
    places.insert(places.begin(), p);
    if (places.size() > MAX_PLACES) places.pop_back();
    savePlaces();
    loaded = false;
    statusLine.clear();
    statusIsError = false;
    refreshWanted = true;
    printf("Weather: now showing %s\n", p.name.c_str());
  }
  drawForecast();
}

static void updateSearch(const Touch &touch) {
  if (touch.pressed && touch.y >= CHOICE_TOP && touch.y < INPUT_TOP) {
    size_t index = (touch.y - CHOICE_TOP) / CHOICE_H;
    if (index < choices.size()) choose(choices[index]);
    return;
  }

  char typed = 0;
  switch (keyboard::update(touch, &typed)) {
    case keyboard::TYPED:
      if (query.size() < 40) query += typed;
      drawInput();
      break;
    case keyboard::BACKSPACE:
      if (!query.empty()) query.pop_back();
      drawInput();
      break;
    case keyboard::ACTION:
      if (query.find_first_not_of(' ') != std::string::npos) search();
      break;
    case keyboard::NONE:
      break;
  }
}

// ---------- app ----------

static void enter() {
  loadPlaces();
  searching = false;
  drawForecast();
  // Refresh on opening, unless what is on show is recent enough.
  refreshWanted = !loaded || statusIsError || lgfx::millis() - lastAttempt > REFRESH_MS;
}

static void update(const Touch &touch) {
  if (searching) {
    updateSearch(touch);
    return;
  }
  if (touch.pressed && touch.y > TITLE_H && touch.y < NOW_TOP) {
    openSearch();
    return;
  }
  if (touch.pressed && loaded && touch.y >= HOURLY_TOP && touch.y < HOURLY_TOP + HOURLY_H) {
    int pages = (hourCount + HOURS_PER_PAGE - 1) / HOURS_PER_PAGE;
    hourPage = (hourPage + 1) % (pages > 0 ? pages : 1);
    lcd.startWrite();
    drawHourly();
    lcd.endWrite();
    return;
  }

  uint32_t sinceAttempt = lgfx::millis() - lastAttempt;
  bool tapped = touch.pressed && touch.y >= NOW_TOP && sinceAttempt > 3000;
  // Retry sooner after a failure than after a successful update.
  uint32_t interval = statusIsError ? 30000UL : REFRESH_MS;
  if (refreshWanted || tapped || sinceAttempt > interval) refresh();
}

const App weatherApp = {"Weather", drawWeatherIcon, enter, update};

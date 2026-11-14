// World Clock: the time now in a list of cities you choose. Each city carries
// its own daylight saving rules, so its time stays right all year without
// needing the internet (beyond setting the device's own clock).

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

#include "apps.h"
#include "keyboard.h"
#include "platform.h"

// ---------- cities ----------

// Each city's timezone is a POSIX TZ string: the standard offset and, where
// the city has daylight saving, the dates it starts and ends. For example
// "AEST-10AEDT,M10.1.0,M4.1.0/3" is ten hours ahead of UTC, with daylight
// saving from the first Sunday of October to the first Sunday of April.
// Cities whose rules cannot be written reliably this way are left out.
struct City {
  const char *name;
  const char *country;
  const char *timezone;
};

#define TZ_AUS_EAST "AEST-10AEDT,M10.1.0,M4.1.0/3"
#define TZ_NZ "NZST-12NZDT,M9.5.0,M4.1.0/3"
#define TZ_US_PACIFIC "PST8PDT,M3.2.0,M11.1.0"
#define TZ_US_MOUNTAIN "MST7MDT,M3.2.0,M11.1.0"
#define TZ_US_CENTRAL "CST6CDT,M3.2.0,M11.1.0"
#define TZ_US_EASTERN "EST5EDT,M3.2.0,M11.1.0"
#define TZ_UK "GMT0BST,M3.5.0/1,M10.5.0"
#define TZ_EUROPE_CENTRAL "CET-1CEST,M3.5.0,M10.5.0/3"
#define TZ_EUROPE_EAST "EET-2EEST,M3.5.0/3,M10.5.0/4"

static const City CITIES[] = {
    {"Adelaide", "Australia", "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
    {"Amsterdam", "Netherlands", TZ_EUROPE_CENTRAL},
    {"Anchorage", "USA", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"Athens", "Greece", TZ_EUROPE_EAST},
    {"Auckland", "New Zealand", TZ_NZ},
    {"Bangkok", "Thailand", "<+07>-7"},
    {"Beijing", "China", "CST-8"},
    {"Berlin", "Germany", TZ_EUROPE_CENTRAL},
    {"Bogota", "Colombia", "<-05>5"},
    {"Boston", "USA", TZ_US_EASTERN},
    {"Brisbane", "Australia", "AEST-10"},
    {"Brussels", "Belgium", TZ_EUROPE_CENTRAL},
    {"Buenos Aires", "Argentina", "<-03>3"},
    {"Canberra", "Australia", TZ_AUS_EAST},
    {"Chicago", "USA", TZ_US_CENTRAL},
    {"Copenhagen", "Denmark", TZ_EUROPE_CENTRAL},
    {"Darwin", "Australia", "ACST-9:30"},
    {"Delhi", "India", "IST-5:30"},
    {"Denver", "USA", TZ_US_MOUNTAIN},
    {"Dhaka", "Bangladesh", "<+06>-6"},
    {"Dubai", "UAE", "<+04>-4"},
    {"Dublin", "Ireland", "GMT0IST,M3.5.0/1,M10.5.0"},
    {"Helsinki", "Finland", TZ_EUROPE_EAST},
    {"Hobart", "Australia", TZ_AUS_EAST},
    {"Hong Kong", "China", "HKT-8"},
    {"Honolulu", "USA", "HST10"},
    {"Istanbul", "Turkey", "<+03>-3"},
    {"Jakarta", "Indonesia", "WIB-7"},
    {"Johannesburg", "South Africa", "SAST-2"},
    {"Karachi", "Pakistan", "PKT-5"},
    {"Kathmandu", "Nepal", "<+0545>-5:45"},
    {"Kuala Lumpur", "Malaysia", "<+08>-8"},
    {"Lagos", "Nigeria", "WAT-1"},
    {"Lima", "Peru", "<-05>5"},
    {"Lisbon", "Portugal", "WET0WEST,M3.5.0/1,M10.5.0"},
    {"London", "UK", TZ_UK},
    {"Los Angeles", "USA", TZ_US_PACIFIC},
    {"Madrid", "Spain", TZ_EUROPE_CENTRAL},
    {"Manila", "Philippines", "PST-8"},
    {"Melbourne", "Australia", TZ_AUS_EAST},
    {"Mexico City", "Mexico", "CST6"},
    {"Miami", "USA", TZ_US_EASTERN},
    {"Moscow", "Russia", "MSK-3"},
    {"Mumbai", "India", "IST-5:30"},
    {"Nairobi", "Kenya", "EAT-3"},
    {"New York", "USA", TZ_US_EASTERN},
    {"Oslo", "Norway", TZ_EUROPE_CENTRAL},
    {"Paris", "France", TZ_EUROPE_CENTRAL},
    {"Perth", "Australia", "AWST-8"},
    {"Phoenix", "USA", "MST7"},
    {"Prague", "Czechia", TZ_EUROPE_CENTRAL},
    {"Reykjavik", "Iceland", "GMT0"},
    {"Riyadh", "Saudi Arabia", "<+03>-3"},
    {"Rome", "Italy", TZ_EUROPE_CENTRAL},
    {"San Francisco", "USA", TZ_US_PACIFIC},
    {"Sao Paulo", "Brazil", "<-03>3"},
    {"Seattle", "USA", TZ_US_PACIFIC},
    {"Seoul", "South Korea", "KST-9"},
    {"Shanghai", "China", "CST-8"},
    {"Singapore", "Singapore", "<+08>-8"},
    {"Stockholm", "Sweden", TZ_EUROPE_CENTRAL},
    {"Sydney", "Australia", TZ_AUS_EAST},
    {"Taipei", "Taiwan", "CST-8"},
    {"Tokyo", "Japan", "JST-9"},
    {"Toronto", "Canada", TZ_US_EASTERN},
    {"UTC", "Universal time", "UTC0"},
    {"Vienna", "Austria", TZ_EUROPE_CENTRAL},
    {"Warsaw", "Poland", TZ_EUROPE_CENTRAL},
    {"Washington", "USA", TZ_US_EASTERN},
    {"Wellington", "New Zealand", TZ_NZ},
    {"Zurich", "Switzerland", TZ_EUROPE_CENTRAL},
};
static const int CITY_COUNT = sizeof(CITIES) / sizeof(CITIES[0]);

// ---------- layout ----------

static const int MARGIN = 12;
static const int LIST_TOP = TITLE_H + 2;
static const int ROW_H = 60;
static const size_t MAX_CHOSEN = 6;  // as many as fit on the screen
static const int ADD_TOP = LIST_TOP + (int)MAX_CHOSEN * ROW_H;

static const lgfx::IFont *const FONT_BODY = &fonts::DejaVu12;
static const lgfx::IFont *const FONT_SMALL = &fonts::DejaVu9;

// ---------- state ----------

static std::vector<int> chosen;  // indexes into CITIES, in the order shown
static bool citiesLoaded = false;
static time_t shownMinute = -1;

static bool adding = false;  // the "add a city" screen is open
static std::string filter;
static std::vector<int> matches;

// ---------- time ----------

static bool clockIsSet() { return time(nullptr) > 1600000000; }

// The date and time in a timezone, by briefly switching the C library to it.
static struct tm timeIn(const char *timezone, time_t when) {
  const char *current = getenv("TZ");
  bool hadTimezone = current != nullptr;
  std::string saved = hadTimezone ? current : "";

  setenv("TZ", timezone, 1);
  tzset();
  struct tm result;
  localtime_r(&when, &result);

  if (hadTimezone) {
    setenv("TZ", saved.c_str(), 1);
  } else {
    unsetenv("TZ");
  }
  tzset();
  return result;
}

// A date and time as a count of minutes, for comparing two places.
static long minutesOf(const struct tm &t) {
  int y = t.tm_year + 1900, m = t.tm_mon + 1;
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  long yoe = y - era * 400;
  long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + t.tm_mday - 1;
  long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long day = era * 146097 + doe - 719468;
  return day * 1440 + t.tm_hour * 60 + t.tm_min;
}

// "Tomorrow, 11 hrs ahead": how a city's time compares with the time here.
static std::string comparison(const struct tm &there, const struct tm &here) {
  long dayThere = minutesOf(there) / 1440, dayHere = minutesOf(here) / 1440;
  // Dates before 1970 would round the wrong way, but the clock is never set there.
  std::string text = dayThere > dayHere ? "Tomorrow" : dayThere < dayHere ? "Yesterday" : "Today";

  long difference = minutesOf(there) - minutesOf(here);
  if (difference == 0) return text + ", same time";
  long size = difference < 0 ? -difference : difference;
  char buf[32];
  if (size % 60 == 0) {
    snprintf(buf, sizeof(buf), ", %ld hr%s ", size / 60, size == 60 ? "" : "s");
  } else {
    snprintf(buf, sizeof(buf), ", %ld.%02ld hrs ", size / 60, size % 60 * 100 / 60);
    // 5.50 reads better as 5.5
    std::string s = buf;
    size_t zero = s.find("0 hrs");
    if (zero != std::string::npos) s.erase(zero, 1);
    snprintf(buf, sizeof(buf), "%s", s.c_str());
  }
  return text + buf + (difference > 0 ? "ahead" : "behind");
}

// ---------- saving the choice ----------

static int findCity(const std::string &name) {
  for (int i = 0; i < CITY_COUNT; i++) {
    if (name == CITIES[i].name) return i;
  }
  return -1;
}

static void saveCities() {
  std::string saved;
  for (int index : chosen) saved += (saved.empty() ? "" : ",") + std::string(CITIES[index].name);
  // A lone comma records an empty list, as distinct from never having chosen.
  platformSaveSetting("world_clocks", saved.empty() ? "," : saved);
}

static void loadCities() {
  if (citiesLoaded) return;
  citiesLoaded = true;
  std::string saved = platformLoadSetting("world_clocks");
  if (saved.empty()) saved = "Sydney,Tokyo,London,New York";
  size_t at = 0;
  while (at < saved.size() && chosen.size() < MAX_CHOSEN) {
    size_t comma = saved.find(',', at);
    if (comma == std::string::npos) comma = saved.size();
    int index = findCity(saved.substr(at, comma - at));
    if (index >= 0) chosen.push_back(index);
    at = comma + 1;
  }
}

// ---------- drawing: the clocks ----------

static void drawRow(int row, const struct tm &here, time_t now) {
  const City &city = CITIES[chosen[row]];
  struct tm there = timeIn(city.timezone, now);
  int y = LIST_TOP + row * ROW_H;

  // A sun or moon shows whether it is day or night there.
  bool daytime = there.tm_hour >= 6 && there.tm_hour < 18;
  if (daytime) {
    lcd.fillSmoothCircle(22, y + 20, 7, 0xFFD60Au);
  } else {
    lcd.fillSmoothCircle(22, y + 20, 8, 0xF4E9B0u);
    lcd.fillSmoothCircle(26, y + 17, 7, colour::background);
  }

  lcd.setTextDatum(top_left);
  lcd.setTextColor(colour::text);
  lcd.drawString(city.name, 42, y + 10, &fonts::DejaVu18);
  lcd.setTextColor(colour::dim);
  lcd.drawString(comparison(there, here).c_str(), 42, y + 36, FONT_SMALL);

  char buf[8];
  snprintf(buf, sizeof(buf), "%d:%02d", there.tm_hour, there.tm_min);
  const int right = lcd.width() - 40;
  lcd.setTextDatum(top_right);
  lcd.setTextColor(colour::text);
  lcd.drawString(buf, right, y + 8, &fonts::DejaVu24);
  if (there.tm_isdst > 0) {
    lcd.setTextColor(colour::orange);
    lcd.drawString("daylight saving", right, y + 37, FONT_SMALL);
  }

  // A cross at the right-hand end removes the city.
  int cx = lcd.width() - 18, cy = y + ROW_H / 2 - 2;
  lcd.drawWideLine(cx - 4, cy - 4, cx + 4, cy + 4, 0.9f, colour::dim);
  lcd.drawWideLine(cx - 4, cy + 4, cx + 4, cy - 4, 0.9f, colour::dim);

  lcd.drawFastHLine(MARGIN, y + ROW_H - 1, lcd.width() - 2 * MARGIN, colour::bar);
}

static void drawClocks() {
  lcd.startWrite();
  lcd.fillRect(0, TITLE_H, lcd.width(), lcd.height() - TITLE_H, colour::background);

  if (!clockIsSet()) {
    lcd.setTextDatum(middle_center);
    lcd.setTextColor(colour::dim);
    lcd.drawString("Waiting for the clock...", lcd.width() / 2, lcd.height() / 2 - 12,
                   &fonts::DejaVu18);
    lcd.drawString("It sets itself once WiFi has joined.", lcd.width() / 2, lcd.height() / 2 + 14,
                   FONT_BODY);
    lcd.endWrite();
    return;
  }

  time_t now = time(nullptr);
  struct tm here;
  localtime_r(&now, &here);
  shownMinute = now / 60;
  for (size_t row = 0; row < chosen.size(); row++) drawRow((int)row, here, now);

  int cy = ADD_TOP + (lcd.height() - ADD_TOP) / 2;
  lcd.setTextDatum(middle_center);
  if (chosen.size() < MAX_CHOSEN) {
    lcd.fillSmoothRoundRect(lcd.width() / 2 - 70, cy - 17, 140, 34, 17, colour::rule);
    lcd.setTextColor(colour::text);
    lcd.drawString("+ Add a city", lcd.width() / 2, cy, FONT_BODY);
  } else {
    lcd.setTextColor(colour::dim);
    lcd.drawString("Remove a city to add another", lcd.width() / 2, cy, FONT_BODY);
  }
  if (chosen.empty()) {
    lcd.setTextColor(colour::dim);
    lcd.drawString("No cities chosen", lcd.width() / 2, LIST_TOP + 80, &fonts::DejaVu18);
  }
  lcd.endWrite();
}

// ---------- adding a city ----------

static const int CHOICE_TOP = TITLE_H + 44;
static const int CHOICE_H = 36;
static const int MAX_MATCHES = 5;
static const int INPUT_H = 40;
static const int INPUT_TOP = keyboard::TOP - INPUT_H;

static bool isChosen(int index) {
  for (int c : chosen) {
    if (c == index) return true;
  }
  return false;
}

// True if the typed text appears anywhere in s, ignoring capitals.
static bool contains(const char *s, const std::string &typed) {
  std::string text;
  for (; *s; s++) text += (char)tolower((unsigned char)*s);
  return text.find(typed) != std::string::npos;
}

// Lists cities whose name or country matches what has been typed so far.
static void findMatches() {
  std::string typed;
  for (char c : filter) typed += (char)tolower((unsigned char)c);
  matches.clear();
  // Cities whose names start with the text come first, then other matches.
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < CITY_COUNT && (int)matches.size() < MAX_MATCHES; i++) {
      if (isChosen(i)) continue;
      std::string name;
      for (const char *s = CITIES[i].name; *s; s++) name += (char)tolower((unsigned char)*s);
      bool starts = name.compare(0, typed.size(), typed) == 0;
      bool anywhere = contains(CITIES[i].name, typed) || contains(CITIES[i].country, typed);
      if (pass == 0 ? starts : (anywhere && !starts)) matches.push_back(i);
    }
  }
}

static void drawInput() {
  lcd.startWrite();
  lcd.fillRect(0, INPUT_TOP, lcd.width(), INPUT_H, colour::background);
  lcd.fillSmoothRoundRect(6, INPUT_TOP + 4, lcd.width() - 12, INPUT_H - 8, 10, colour::bar);
  int cy = INPUT_TOP + INPUT_H / 2, x = 18;
  lcd.setTextDatum(middle_left);
  if (filter.empty()) {
    lcd.setTextColor(colour::dim);
    lcd.drawString("City or country", x + 4, cy, &fonts::DejaVu18);
  } else {
    lcd.setTextColor(colour::text);
    lcd.drawString(filter.c_str(), x, cy, &fonts::DejaVu18);
    x += lcd.textWidth(filter.c_str(), &fonts::DejaVu18) + 2;
  }
  lcd.fillRect(x, cy - 10, 2, 20, colour::green);
  lcd.endWrite();
}

static void drawMatches() {
  lcd.startWrite();
  lcd.fillRect(0, CHOICE_TOP, lcd.width(), INPUT_TOP - CHOICE_TOP, colour::background);
  for (size_t i = 0; i < matches.size(); i++) {
    const City &city = CITIES[matches[i]];
    int y = CHOICE_TOP + (int)i * CHOICE_H;
    lcd.fillSmoothRoundRect(6, y + 2, lcd.width() - 12, CHOICE_H - 4, 10, colour::bar);
    lcd.setTextDatum(middle_left);
    lcd.setTextColor(colour::text);
    lcd.drawString(city.name, 18, y + CHOICE_H / 2, &fonts::DejaVu18);
    lcd.setTextDatum(middle_right);
    lcd.setTextColor(colour::dim);
    lcd.drawString(city.country, lcd.width() - 18, y + CHOICE_H / 2, FONT_BODY);
  }
  if (matches.empty()) {
    lcd.setTextDatum(top_left);
    lcd.setTextColor(colour::dim);
    lcd.drawString("No city in the list matches that.", MARGIN, CHOICE_TOP + 10, FONT_BODY);
  }
  lcd.endWrite();
}

static void openAdd() {
  adding = true;
  filter.clear();
  findMatches();

  lcd.startWrite();
  lcd.fillRect(0, TITLE_H, lcd.width(), lcd.height() - TITLE_H, colour::background);
  lcd.setTextDatum(middle_left);
  lcd.setTextColor(colour::dim);
  lcd.drawString("Type to find a city, then tap it", MARGIN, TITLE_H + 23, FONT_BODY);
  const int buttonW = 66;
  int buttonX = lcd.width() - 8 - buttonW;
  lcd.fillSmoothRoundRect(buttonX, TITLE_H + 10, buttonW, 26, 13, colour::rule);
  lcd.setTextDatum(middle_center);
  lcd.setTextColor(colour::text);
  lcd.drawString("Cancel", buttonX + buttonW / 2, TITLE_H + 23, FONT_BODY);
  lcd.endWrite();

  drawMatches();
  drawInput();
  keyboard::open("Add", true);
}

static void closeAdd(int index) {
  adding = false;
  if (index >= 0 && !isChosen(index) && chosen.size() < MAX_CHOSEN) {
    chosen.push_back(index);
    saveCities();
    printf("World Clock: added %s\n", CITIES[index].name);
  }
  drawClocks();
}

static void updateAdd(const Touch &touch) {
  if (touch.pressed && touch.y > TITLE_H && touch.y < CHOICE_TOP && touch.x > lcd.width() - 90) {
    closeAdd(-1);
    return;
  }
  if (touch.pressed && touch.y >= CHOICE_TOP && touch.y < INPUT_TOP) {
    size_t row = (touch.y - CHOICE_TOP) / CHOICE_H;
    if (row < matches.size()) closeAdd(matches[row]);
    return;
  }

  char typed = 0;
  switch (keyboard::update(touch, &typed)) {
    case keyboard::TYPED:
      if (filter.size() < 20) filter += typed;
      break;
    case keyboard::BACKSPACE:
      if (!filter.empty()) filter.pop_back();
      break;
    case keyboard::ACTION:
      // The Add key takes the first match, as tapping it would.
      if (!matches.empty()) closeAdd(matches[0]);
      return;
    case keyboard::NONE:
      return;
  }
  findMatches();
  drawMatches();
  drawInput();
}

// ---------- app ----------

static void enter() {
  loadCities();
  adding = false;
  drawClocks();
}

static void update(const Touch &touch) {
  if (adding) {
    updateAdd(touch);
    return;
  }
  // Redraw when the minute changes, and when the clock is first set.
  if (clockIsSet() && time(nullptr) / 60 != shownMinute) drawClocks();
  if (!touch.pressed || !clockIsSet()) return;

  if (touch.y >= ADD_TOP) {
    if (chosen.size() < MAX_CHOSEN) openAdd();
  } else if (touch.y >= LIST_TOP && touch.x > lcd.width() - 40) {
    size_t row = (touch.y - LIST_TOP) / ROW_H;
    if (row < chosen.size()) {
      printf("World Clock: removed %s\n", CITIES[chosen[row]].name);
      chosen.erase(chosen.begin() + row);
      saveCities();
      drawClocks();
    }
  }
}

const App worldClockApp = {"World Clock", drawWorldClockIcon, enter, update};

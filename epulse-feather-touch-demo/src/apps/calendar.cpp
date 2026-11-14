// Calendar: a month at a time, with today marked. Tap a day to see its
// events, add one with the keyboard, or delete one. Events are saved on the
// device, so they survive a restart.

#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "apps.h"
#include "keyboard.h"
#include "platform.h"

// ---------- layout ----------

static const int MARGIN = 12;
static const int MONTH_TOP = TITLE_H;
static const int MONTH_H = 40;
static const int WEEKDAYS_TOP = MONTH_TOP + MONTH_H;
static const int GRID_TOP = WEEKDAYS_TOP + 18;
static const int CELL_W = 45;
static const int CELL_H = 40;
static const int GRID_LEFT = (SCREEN_W - 7 * CELL_W) / 2;
static const int PANEL_TOP = GRID_TOP + 6 * CELL_H + 4;
static const int EVENTS_TOP = PANEL_TOP + 34;
static const int EVENT_H = 26;

static const lgfx::IFont *const FONT_BODY = &fonts::DejaVu12;

static const uint32_t COLOUR_TODAY = 0xFF453Au;
static const uint32_t COLOUR_EVENT = 0xFF9F0Au;

static const char *const MONTHS[] = {"January", "February", "March",     "April",   "May",      "June",
                                     "July",    "August",   "September", "October", "November", "December"};
static const char *const WEEKDAYS[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday",
                                       "Friday", "Saturday"};

// ---------- dates ----------

// Days since 1970-01-01 for a date.
static long dayNumber(int y, int m, int d) {
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  long yoe = y - era * 400;
  long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

static int weekdayOf(long day) { return (int)(((day + 4) % 7 + 7) % 7); }  // 0 is Sunday

static int daysInMonth(int y, int m) {
  return (int)(dayNumber(m == 12 ? y + 1 : y, m == 12 ? 1 : m + 1, 1) - dayNumber(y, m, 1));
}

static bool clockIsSet() { return time(nullptr) > 1600000000; }

// Today's date where the device is. Returns false until the clock is set.
static bool today(int &y, int &m, int &d) {
  if (!clockIsSet()) return false;
  time_t now = time(nullptr);
  struct tm local;
  localtime_r(&now, &local);
  y = local.tm_year + 1900;
  m = local.tm_mon + 1;
  d = local.tm_mday;
  return true;
}

// ---------- events ----------

struct Event {
  long day;  // day number, as from dayNumber()
  std::string text;
};

static const size_t MAX_EVENTS = 40;       // in total, to fit the device's settings storage
static const size_t MAX_EVENTS_PER_DAY = 4;  // as many as fit on the screen
static const size_t MAX_TEXT = 40;

static std::vector<Event> events;
static bool eventsLoaded = false;

// Saved as "day|text;day|text", so those two characters are kept out of the text.
static void saveEvents() {
  std::string saved;
  for (auto &e : events) {
    saved += (saved.empty() ? "" : ";") + std::to_string(e.day) + "|" + e.text;
  }
  platformSaveSetting("calendar", saved);
}

static void loadEvents() {
  if (eventsLoaded) return;
  eventsLoaded = true;
  std::string saved = platformLoadSetting("calendar");
  size_t at = 0;
  while (at < saved.size() && events.size() < MAX_EVENTS) {
    size_t end = saved.find(';', at);
    if (end == std::string::npos) end = saved.size();
    std::string entry = saved.substr(at, end - at);
    size_t bar = entry.find('|');
    if (bar != std::string::npos && bar + 1 < entry.size()) {
      events.push_back({atol(entry.c_str()), entry.substr(bar + 1)});
    }
    at = end + 1;
  }
}

static int countOn(long day) {
  int count = 0;
  for (auto &e : events) count += e.day == day;
  return count;
}

// ---------- state ----------

static int shownYear = 0, shownMonth = 0;  // the month on show; 0 until the clock is set
static long selectedDay = 0;
static long todayDay = 0;
static time_t lastCheck = 0;

static bool adding = false;  // the "new event" screen is open
static std::string draft;
static std::string notice;  // a short message shown in place of the event list

// ---------- drawing: month ----------

static std::string fit(std::string s, int maxWidth, const lgfx::IFont *font) {
  if (lcd.textWidth(s.c_str(), font) <= maxWidth) return s;
  while (s.size() > 1 && lcd.textWidth((s + "..").c_str(), font) > maxWidth) s.pop_back();
  return s + "..";
}

static void drawArrow(int cx, int cy, bool pointsLeft) {
  int tip = pointsLeft ? -5 : 5;
  lcd.drawWideLine(cx - tip, cy - 8, cx + tip, cy, 1.2f, colour::orange);
  lcd.drawWideLine(cx + tip, cy, cx - tip, cy + 8, 1.2f, colour::orange);
}

// The first day number shown in the grid: the Monday on or before the 1st.
static long gridStart() {
  long first = dayNumber(shownYear, shownMonth, 1);
  return first - (weekdayOf(first) + 6) % 7;
}

static void drawMonth() {
  lcd.fillRect(0, MONTH_TOP, lcd.width(), PANEL_TOP - MONTH_TOP, colour::background);

  int cy = MONTH_TOP + MONTH_H / 2;
  drawArrow(30, cy, true);
  drawArrow(lcd.width() - 30, cy, false);
  lcd.setTextDatum(middle_center);
  lcd.setTextColor(colour::text);
  lcd.drawString((std::string(MONTHS[shownMonth - 1]) + " " + std::to_string(shownYear)).c_str(),
                 lcd.width() / 2, cy, &fonts::DejaVu18);

  // Weeks run Monday to Sunday.
  static const char *const LETTERS[] = {"M", "T", "W", "T", "F", "S", "S"};
  for (int col = 0; col < 7; col++) {
    lcd.setTextColor(col >= 5 ? colour::rule : colour::dim);
    lcd.drawString(LETTERS[col], GRID_LEFT + col * CELL_W + CELL_W / 2, WEEKDAYS_TOP + 8, FONT_BODY);
  }

  long start = gridStart();
  long first = dayNumber(shownYear, shownMonth, 1);
  long last = first + daysInMonth(shownYear, shownMonth) - 1;
  for (int i = 0; i < 42; i++) {
    long day = start + i;
    int cx = GRID_LEFT + (i % 7) * CELL_W + CELL_W / 2;
    int y = GRID_TOP + (i / 7) * CELL_H + CELL_H / 2;
    bool inMonth = day >= first && day <= last;
    bool isToday = day == todayDay, isSelected = day == selectedDay;

    // Today is a red disc; the selected day has a ring, or a grey disc.
    if (isToday) {
      if (isSelected) lcd.fillSmoothCircle(cx, y, 19, colour::text);
      lcd.fillSmoothCircle(cx, y, isSelected ? 17 : 18, COLOUR_TODAY);
    } else if (isSelected) {
      lcd.fillSmoothCircle(cx, y, 18, colour::rule);
    }

    // The number shown is the day of whichever month the square falls in.
    int number = inMonth ? (int)(day - first + 1)
                 : day < first ? daysInMonth(shownMonth == 1 ? shownYear - 1 : shownYear,
                                             shownMonth == 1 ? 12 : shownMonth - 1) - (int)(first - day) + 1
                               : (int)(day - last);
    lcd.setTextDatum(middle_center);
    lcd.setTextColor(isToday ? colour::text : !inMonth ? colour::rule
                              : i % 7 >= 5 ? colour::dim : colour::text);
    lcd.drawString(std::to_string(number).c_str(), cx, y - 1, &fonts::DejaVu18);

    if (countOn(day) > 0) {  // a dot under the number marks a day with events
      lcd.fillSmoothCircle(cx, y + 13, 2, isToday ? colour::text : inMonth ? COLOUR_EVENT : colour::rule);
    }
  }
}

// ---------- drawing: the selected day ----------

static std::string dayTitle(long day, bool shortForm) {
  // Work the date back out from the grid, which always contains the day.
  long first = dayNumber(shownYear, shownMonth, 1);
  int y = shownYear, m = shownMonth;
  long d = day - first + 1;
  if (d < 1) {
    m = m == 1 ? 12 : m - 1;
    if (m == 12) y--;
    d += daysInMonth(y, m);
  } else if (d > daysInMonth(y, m)) {
    d -= daysInMonth(y, m);
    m = m == 12 ? 1 : m + 1;
  }
  std::string weekday = WEEKDAYS[weekdayOf(day)], month = MONTHS[m - 1];
  if (shortForm) {
    weekday.resize(3);
    month.resize(3);
  }
  return weekday + " " + std::to_string(d) + " " + month;
}

static void drawPanel() {
  lcd.fillRect(0, PANEL_TOP, lcd.width(), lcd.height() - PANEL_TOP, colour::background);
  lcd.drawFastHLine(0, PANEL_TOP, lcd.width(), colour::rule);

  const int buttonW = 58;
  int buttonX = lcd.width() - MARGIN - buttonW;
  lcd.setTextDatum(middle_left);
  lcd.setTextColor(colour::text);
  lcd.drawString(fit(dayTitle(selectedDay, false), buttonX - MARGIN - 6, &fonts::DejaVu18).c_str(),
                 MARGIN, PANEL_TOP + 18, &fonts::DejaVu18);
  lcd.fillSmoothRoundRect(buttonX, PANEL_TOP + 5, buttonW, 26, 13, colour::rule);
  lcd.setTextDatum(middle_center);
  lcd.drawString("+ Add", buttonX + buttonW / 2, PANEL_TOP + 18, FONT_BODY);

  int row = 0;
  for (auto &e : events) {
    if (e.day != selectedDay || row >= (int)MAX_EVENTS_PER_DAY) continue;
    int y = EVENTS_TOP + row * EVENT_H;
    lcd.fillSmoothRoundRect(6, y + 1, lcd.width() - 12, EVENT_H - 3, 8, colour::bar);
    lcd.fillSmoothCircle(19, y + EVENT_H / 2 - 1, 3, COLOUR_EVENT);
    lcd.setTextDatum(middle_left);
    lcd.setTextColor(colour::text);
    lcd.drawString(fit(e.text, lcd.width() - 78, FONT_BODY).c_str(), 30, y + EVENT_H / 2 - 1,
                   FONT_BODY);
    // A cross at the right-hand end deletes the event.
    int cx = lcd.width() - 24, cy = y + EVENT_H / 2 - 1;
    lcd.drawWideLine(cx - 4, cy - 4, cx + 4, cy + 4, 0.9f, colour::dim);
    lcd.drawWideLine(cx - 4, cy + 4, cx + 4, cy - 4, 0.9f, colour::dim);
    row++;
  }
  if (!notice.empty() || row == 0) {
    lcd.setTextDatum(top_left);
    lcd.setTextColor(notice.empty() ? colour::dim : colour::red);
    lcd.drawString(notice.empty() ? "No events" : notice.c_str(), MARGIN,
                   EVENTS_TOP + row * EVENT_H + 6, FONT_BODY);
  }
}

static void drawCalendar() {
  lcd.startWrite();
  if (shownMonth == 0) {
    // Without the date there is nothing sensible to show.
    lcd.fillRect(0, TITLE_H, lcd.width(), lcd.height() - TITLE_H, colour::background);
    lcd.setTextDatum(middle_center);
    lcd.setTextColor(colour::dim);
    lcd.drawString("Waiting for the clock...", lcd.width() / 2, lcd.height() / 2 - 12,
                   &fonts::DejaVu18);
    lcd.drawString("It sets itself once WiFi has joined.", lcd.width() / 2, lcd.height() / 2 + 14,
                   FONT_BODY);
  } else {
    drawMonth();
    drawPanel();
  }
  lcd.endWrite();
}

// ---------- moving around ----------

static void showMonth(int y, int m, long select) {
  shownYear = y;
  shownMonth = m;
  selectedDay = select;
  notice.clear();
  drawCalendar();
}

static void stepMonth(int by) {
  int y = shownYear, m = shownMonth + by;
  if (m < 1) {
    m = 12;
    y--;
  } else if (m > 12) {
    m = 1;
    y++;
  }
  // Land on today if it is in that month, otherwise on the 1st.
  long first = dayNumber(y, m, 1);
  bool hasToday = todayDay >= first && todayDay < first + daysInMonth(y, m);
  showMonth(y, m, hasToday ? todayDay : first);
}

// Keeps "today" right, including when the clock is first set and at midnight.
// Returns true if it changed.
static bool checkToday() {
  time_t now = time(nullptr);
  if (now == lastCheck) return false;
  lastCheck = now;
  int y, m, d;
  if (!today(y, m, d)) return false;
  long day = dayNumber(y, m, d);
  if (day == todayDay) return false;
  todayDay = day;
  if (shownMonth == 0) {
    shownYear = y;
    shownMonth = m;
    selectedDay = day;
  }
  return true;
}

// ---------- adding an event ----------

static const int INPUT_H = 40;
static const int INPUT_TOP = keyboard::TOP - INPUT_H;

static void drawInput() {
  lcd.startWrite();
  lcd.fillRect(0, INPUT_TOP, lcd.width(), INPUT_H, colour::background);
  lcd.fillSmoothRoundRect(6, INPUT_TOP + 4, lcd.width() - 12, INPUT_H - 8, 10, colour::bar);
  int cy = INPUT_TOP + INPUT_H / 2, x = 18;
  lcd.setTextDatum(middle_left);
  if (draft.empty()) {
    lcd.setTextColor(colour::dim);
    lcd.drawString("What is happening?", x + 4, cy, &fonts::DejaVu18);
  } else {
    // Show the end of the text when it is too long for the box.
    std::string shown = draft;
    while (lcd.textWidth(shown.c_str(), &fonts::DejaVu18) > lcd.width() - 44) shown.erase(0, 1);
    lcd.setTextColor(colour::text);
    lcd.drawString(shown.c_str(), x, cy, &fonts::DejaVu18);
    x += lcd.textWidth(shown.c_str(), &fonts::DejaVu18) + 2;
  }
  lcd.fillRect(x, cy - 10, 2, 20, colour::green);
  lcd.endWrite();
}

static void openAdd() {
  if (countOn(selectedDay) >= (int)MAX_EVENTS_PER_DAY) {
    notice = "That day is full. Delete an event first.";
  } else if (events.size() >= MAX_EVENTS) {
    notice = "The calendar is full. Delete an event first.";
  } else {
    notice.clear();
  }
  if (!notice.empty()) {
    lcd.startWrite();
    drawPanel();
    lcd.endWrite();
    return;
  }

  adding = true;
  draft.clear();
  lcd.startWrite();
  lcd.fillRect(0, TITLE_H, lcd.width(), lcd.height() - TITLE_H, colour::background);
  lcd.setTextDatum(top_left);
  lcd.setTextColor(colour::dim);
  lcd.drawString("New event on", MARGIN, TITLE_H + 16, FONT_BODY);
  lcd.setTextColor(colour::text);
  lcd.drawString(dayTitle(selectedDay, false).c_str(), MARGIN, TITLE_H + 36, &fonts::DejaVu18);

  const int buttonW = 70;
  int buttonX = lcd.width() - MARGIN - buttonW;
  lcd.fillSmoothRoundRect(buttonX, TITLE_H + 10, buttonW, 26, 13, colour::rule);
  lcd.setTextDatum(middle_center);
  lcd.drawString("Cancel", buttonX + buttonW / 2, TITLE_H + 23, FONT_BODY);
  lcd.endWrite();

  drawInput();
  keyboard::open("Add", true);
}

static void closeAdd(bool keep) {
  adding = false;
  // Trim spaces from both ends; an empty event is not kept.
  size_t first = draft.find_first_not_of(' ');
  if (keep && first != std::string::npos) {
    std::string text = draft.substr(first, draft.find_last_not_of(' ') - first + 1);
    events.push_back({selectedDay, text});
    saveEvents();
    printf("Calendar: added \"%s\" on %s\n", text.c_str(), dayTitle(selectedDay, true).c_str());
  }
  drawCalendar();
}

static void updateAdd(const Touch &touch) {
  if (touch.pressed && touch.y > TITLE_H && touch.y < TITLE_H + 44 && touch.x > lcd.width() - 100) {
    closeAdd(false);
    return;
  }
  char typed = 0;
  switch (keyboard::update(touch, &typed)) {
    case keyboard::TYPED:
      // '|' and ';' are not on the keyboard, but are refused in case they ever are.
      if (draft.size() < MAX_TEXT && typed != '|' && typed != ';') draft += typed;
      drawInput();
      break;
    case keyboard::BACKSPACE:
      if (!draft.empty()) draft.pop_back();
      drawInput();
      break;
    case keyboard::ACTION:
      closeAdd(true);
      break;
    case keyboard::NONE:
      break;
  }
}

// ---------- app ----------

static void enter() {
  loadEvents();
  adding = false;
  notice.clear();
  lastCheck = 0;
  checkToday();
  // Opening the calendar always starts on today.
  int y, m, d;
  if (today(y, m, d)) {
    shownYear = y;
    shownMonth = m;
    selectedDay = todayDay;
  }
  drawCalendar();
}

static void deleteEvent(int row) {
  int seen = 0;
  for (size_t i = 0; i < events.size(); i++) {
    if (events[i].day != selectedDay) continue;
    if (seen++ == row) {
      printf("Calendar: deleted \"%s\"\n", events[i].text.c_str());
      events.erase(events.begin() + i);
      saveEvents();
      break;
    }
  }
  notice.clear();
  lcd.startWrite();
  drawMonth();  // the day's dot may have gone
  drawPanel();
  lcd.endWrite();
}

static void update(const Touch &touch) {
  if (adding) {
    updateAdd(touch);
    return;
  }
  if (checkToday()) drawCalendar();
  if (shownMonth == 0 || !touch.pressed) return;

  if (touch.y < WEEKDAYS_TOP) {
    // Month heading: arrows at either end, and the name returns to today.
    if (touch.x < 90) {
      stepMonth(-1);
    } else if (touch.x > lcd.width() - 90) {
      stepMonth(1);
    } else {
      int y, m, d;
      if (today(y, m, d)) showMonth(y, m, todayDay);
    }
  } else if (touch.y >= GRID_TOP && touch.y < PANEL_TOP) {
    int col = (touch.x - GRID_LEFT) / CELL_W, row = (touch.y - GRID_TOP) / CELL_H;
    if (touch.x < GRID_LEFT || col > 6 || row > 5) return;
    long day = gridStart() + row * 7 + col;
    long first = dayNumber(shownYear, shownMonth, 1);
    if (day < first) {  // a greyed-out day from the month before or after
      stepMonth(-1);
    } else if (day >= first + daysInMonth(shownYear, shownMonth)) {
      stepMonth(1);
    }
    selectedDay = day;
    notice.clear();
    drawCalendar();
  } else if (touch.y >= PANEL_TOP && touch.y < EVENTS_TOP) {
    if (touch.x > lcd.width() - 90) openAdd();
  } else if (touch.y >= EVENTS_TOP && touch.x > lcd.width() - 50) {
    int row = (touch.y - EVENTS_TOP) / EVENT_H;
    if (row < countOn(selectedDay)) deleteEvent(row);
  }
}

const App calendarApp = {"Calendar", drawCalendarIcon, enter, update};

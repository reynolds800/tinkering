// GitHub: shows a user's contribution calendar for the last year, as on their
// GitHub profile page, with their streaks. The user can be changed by typing a
// name or picking a recent one. It refreshes when opened, every few minutes
// while open, and on a tap.

#include <ArduinoJson.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "apps.h"
#include "keyboard.h"
#include "platform.h"
#include "settings.h"

// ---------- layout ----------

static const int MARGIN = 12;
static const int HEADER_BOTTOM = TITLE_H + 74;  // tapping above this changes the user
static const int GRID_LEFT = 24;
static const int PITCH = 11;  // distance from one day square to the next
static const int CELL = 9;
static const int GRID_A_TOP = TITLE_H + 94;
static const int GRID_B_TOP = GRID_A_TOP + 7 * PITCH + 24;
static const int FOOTER_Y = SCREEN_H - 16;

static const lgfx::IFont *const FONT_BODY = &fonts::DejaVu12;
static const lgfx::IFont *const FONT_SMALL = &fonts::DejaVu9;

// From no contributions to the most, as on GitHub's dark theme.
static const uint32_t LEVEL_COLOURS[5] = {0x161B22u, 0x0E4429u, 0x006D32u, 0x26A641u, 0x39D353u};

// ---------- state ----------

static const int MAX_WEEKS = 53;
static const int WEEKS_PER_ROW = 26;
static const size_t MAX_USERS = 5;
static const size_t MAX_NAME = 39;  // GitHub's limit

static std::vector<std::string> users;  // most recently chosen first; users[0] is on show
static std::string savedUsers;          // the list as last saved
static bool choosing = false;           // the "change user" screen is open
static std::string draft;

static int8_t grid[MAX_WEEKS][7];  // contribution level 0-4, or -1 for no such day
static int weeks = 0;
static long firstSunday = 0;  // day number of grid[0][0]
static std::string totalText;
static int currentStreak = 0, longestStreak = 0, activeDays = 0;
static bool loaded = false;

static std::string profileLine;
static std::string statusLine;
static bool statusIsError = false;
static bool refreshWanted = false;
static uint32_t lastAttempt = 0;

static const std::string &user() { return users[0]; }

// ---------- dates ----------

// Days since 1970-01-01 for a date, and back again.
static long dayNumber(int y, int m, int d) {
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  long yoe = y - era * 400;
  long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

static int monthOf(long day) {
  day += 719468;
  long era = (day >= 0 ? day : day - 146096) / 146097;
  long doe = day - era * 146097;
  long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  long mp = (5 * doy + 2) / 153;
  return (int)(mp < 10 ? mp + 3 : mp - 9);  // 1 to 12
}

static int weekdayOf(long day) { return (int)(((day + 4) % 7 + 7) % 7); }  // 0 is Sunday

// ---------- reading the calendar page ----------

// Picks the day squares out of GitHub's contribution calendar page as it
// downloads. Each square looks like
//   <td ... data-date="2026-11-14" ... data-level="3" ...>
// and the year's total is the first number inside the page's heading.
struct CalendarReader {
  std::string pending;  // downloaded text not yet dealt with
  std::vector<std::pair<long, int8_t>> days;
  std::string total;
  bool finished = false;

  bool feed(const char *data, size_t size) {
    pending.append(data, size);

    static const char HEADING[] = "js-contribution-activity-description";
    size_t heading = std::string::npos;
    if (total.empty()) {
      heading = pending.find(HEADING);
      size_t start = heading == std::string::npos ? heading : pending.find('>', heading);
      if (start != std::string::npos && pending.size() > start + 80) {
        size_t at = pending.find_first_not_of(" \r\n\t", start + 1);
        while (at < pending.size() && (isdigit((unsigned char)pending[at]) || pending[at] == ',')) {
          total += pending[at++];
        }
        if (total.empty()) total = "?";
        heading = std::string::npos;
      }
    }

    size_t done = 0;                       // everything before this has been dealt with
    size_t incomplete = std::string::npos;  // a square that needs more of the page
    while (true) {
      size_t date = pending.find("data-date=\"", done);
      if (date == std::string::npos) break;
      size_t level = pending.find("data-level=\"", date);
      if (level == std::string::npos || level + 13 > pending.size()) {
        if (pending.size() - date < 600) incomplete = date;
        break;
      }
      int y, m, d;
      if (sscanf(pending.c_str() + date + 11, "%d-%d-%d", &y, &m, &d) == 3) {
        char c = pending[level + 12];
        days.emplace_back(dayNumber(y, m, d), (int8_t)(c >= '0' && c <= '4' ? c - '0' : 0));
      }
      done = level + 13;
    }
    if (pending.find("</table>", done) != std::string::npos) finished = true;

    // Keep only what is still needed: an unfinished square or heading, or
    // else a short tail in case something is split across two pieces.
    size_t keep = incomplete != std::string::npos ? incomplete
                                                  : pending.size() > 48 ? pending.size() - 48 : 0;
    if (keep < done) keep = done;
    if (heading != std::string::npos && heading < keep) keep = heading;
    pending.erase(0, keep);
    return !finished;  // stop downloading once the calendar has been read
  }
};

// Arranges the days into weeks and works out the streaks.
static void buildGrid(const CalendarReader &reader) {
  memset(grid, -1, sizeof(grid));
  weeks = 0;
  currentStreak = longestStreak = activeDays = 0;
  if (reader.days.empty()) return;

  long first = reader.days[0].first, last = first;
  for (auto &day : reader.days) {
    if (day.first < first) first = day.first;
    if (day.first > last) last = day.first;
  }
  firstSunday = first - weekdayOf(first);
  weeks = (int)((last - firstSunday) / 7 + 1);
  if (weeks > MAX_WEEKS) {  // show only the most recent weeks
    firstSunday += 7L * (weeks - MAX_WEEKS);
    weeks = MAX_WEEKS;
  }
  for (auto &day : reader.days) {
    long offset = day.first - firstSunday;
    if (offset >= 0) grid[offset / 7][offset % 7] = day.second;
  }

  int run = 0;
  for (long offset = 0; offset <= last - firstSunday; offset++) {
    int8_t level = grid[offset / 7][offset % 7];
    if (level > 0) {
      activeDays++;
      if (++run > longestStreak) longestStreak = run;
    } else {
      // A day with nothing yet does not break the streak if it is today.
      if (offset < last - firstSunday) run = 0;
    }
  }
  currentStreak = run;
}

// ---------- drawing: calendar ----------

static std::string fit(std::string s, int maxWidth, const lgfx::IFont *font) {
  if (lcd.textWidth(s.c_str(), font) <= maxWidth) return s;
  while (s.size() > 1 && lcd.textWidth((s + "..").c_str(), font) > maxWidth) s.pop_back();
  return s + "..";
}

static void drawFooter() {
  lcd.fillRect(0, FOOTER_Y - 4, lcd.width(), lcd.height() - FOOTER_Y + 4, colour::background);
  lcd.setTextDatum(top_left);
  lcd.setTextColor(statusIsError ? colour::red : colour::dim);
  lcd.drawString(fit(statusLine, lcd.width() - 2 * MARGIN, FONT_SMALL).c_str(), MARGIN, FOOTER_Y,
                 FONT_SMALL);
}

static void setStatus(const std::string &message, bool isError = false) {
  statusLine = message;
  statusIsError = isError;
  printf("GitHub: %s\n", message.c_str());
  drawFooter();
}

// Draws some of the weeks as a block: month names, day names, then squares.
static void drawWeeks(int firstWeek, int count, int top) {
  static const char *const MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  lcd.setTextDatum(top_left);
  lcd.setTextColor(colour::dim);

  // A month is named above the first week that starts in it.
  int lastLabelX = -100;
  for (int i = 0; i < count; i++) {
    int week = firstWeek + i;
    int month = monthOf(firstSunday + 7L * week);
    bool starts = week == 0 || month != monthOf(firstSunday + 7L * (week - 1));
    int x = GRID_LEFT + i * PITCH;
    if ((starts || i == 0) && x - lastLabelX >= 26 && i <= count - 2) {
      // The first column only gets a name if the month runs on for a while.
      if (i == 0 && !starts && monthOf(firstSunday + 7L * (week + 2)) != month) continue;
      lcd.drawString(MONTHS[month - 1], x, top - 13, FONT_SMALL);
      lastLabelX = x;
    }
  }

  lcd.drawString("Mon", 1, top + 1 * PITCH, FONT_SMALL);
  lcd.drawString("Wed", 1, top + 3 * PITCH, FONT_SMALL);
  lcd.drawString("Fri", 1, top + 5 * PITCH, FONT_SMALL);

  for (int i = 0; i < count; i++) {
    for (int day = 0; day < 7; day++) {
      int8_t level = grid[firstWeek + i][day];
      if (level < 0) continue;
      lcd.fillRoundRect(GRID_LEFT + i * PITCH, top + day * PITCH, CELL, CELL, 2,
                        LEVEL_COLOURS[level]);
    }
  }
}

static void drawStat(int column, int value, const char *label) {
  int cx = lcd.width() / 6 + column * (lcd.width() / 3);
  lcd.setTextDatum(top_center);
  lcd.setTextColor(colour::text);
  lcd.drawString(std::to_string(value).c_str(), cx, GRID_B_TOP + 7 * PITCH + 40, &fonts::DejaVu24);
  lcd.setTextColor(colour::dim);
  lcd.drawString(label, cx, GRID_B_TOP + 7 * PITCH + 70, FONT_SMALL);
}

static void drawCalendar() {
  lcd.startWrite();
  lcd.fillRect(0, TITLE_H, lcd.width(), lcd.height() - TITLE_H, colour::background);

  // Header: who is on show, and a button to change that.
  const int buttonW = 70;
  int buttonX = lcd.width() - MARGIN - buttonW;
  lcd.setTextDatum(top_left);
  lcd.setTextColor(colour::text);
  lcd.drawString(fit("@" + user(), buttonX - MARGIN - 8, &fonts::DejaVu18).c_str(), MARGIN,
                 TITLE_H + 10, &fonts::DejaVu18);
  lcd.fillSmoothRoundRect(buttonX, TITLE_H + 6, buttonW, 26, 13, colour::rule);
  lcd.setTextDatum(middle_center);
  lcd.drawString("Change", buttonX + buttonW / 2, TITLE_H + 19, FONT_BODY);

  lcd.setTextDatum(top_left);
  lcd.setTextColor(colour::dim);
  lcd.drawString(fit(profileLine, lcd.width() - 2 * MARGIN, FONT_BODY).c_str(), MARGIN,
                 TITLE_H + 36, FONT_BODY);

  if (loaded && weeks > 0) {
    lcd.setTextColor(colour::text);
    lcd.drawString((totalText + " contributions in the last year").c_str(), MARGIN, TITLE_H + 56,
                   FONT_BODY);

    // The year is split in two so the squares are big enough to see:
    // the older weeks above, the most recent weeks below.
    int older = weeks > WEEKS_PER_ROW + 1 ? weeks - WEEKS_PER_ROW : 0;
    if (older) drawWeeks(0, older, GRID_A_TOP);
    drawWeeks(older, weeks - older, older ? GRID_B_TOP : GRID_A_TOP);

    // Key to the colours.
    int y = GRID_B_TOP + 7 * PITCH + 12;
    int x = lcd.width() - MARGIN;
    lcd.setTextDatum(top_right);
    lcd.setTextColor(colour::dim);
    lcd.drawString("More", x, y, FONT_SMALL);
    x -= lcd.textWidth("More", FONT_SMALL) + 6 + 5 * PITCH;
    for (int level = 0; level < 5; level++) {
      lcd.fillRoundRect(x + level * PITCH, y - 1, CELL, CELL, 2, LEVEL_COLOURS[level]);
    }
    lcd.drawString("Less", x - 6, y, FONT_SMALL);

    drawStat(0, currentStreak, "day streak");
    drawStat(1, longestStreak, "longest streak");
    drawStat(2, activeDays, "active days");
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

static void refresh() {
  refreshWanted = false;
  lastAttempt = lgfx::millis();
  if (!platformOnline()) {
    setStatus("Not connected to " + platformNetworkName() + ", retrying", true);
    return;
  }
  setStatus("Updating...");

  CalendarReader reader;
  int code = platformHttpGet("https://github.com/users/" + user() + "/contributions",
                             [&reader](const char *data, size_t size) {
                               return reader.feed(data, size);
                             });
  if (code != 200 || reader.days.empty()) {
    std::string problem = code == 404  ? "GitHub user " + user() + " not found"
                          : code == 429 ? "GitHub is limiting requests, will retry"
                          : code < 0    ? "Could not reach GitHub"
                          : code != 200 ? "GitHub returned HTTP " + std::to_string(code)
                                        : "Could not read GitHub's calendar";
    // Keep showing what was loaded before; only the footer changes.
    setStatus(problem, true);
    if (!loaded) drawCalendar();
    return;
  }
  buildGrid(reader);
  totalText = reader.total;
  loaded = true;

  // Remember the list of users only once the newest is known to exist, so a
  // mistyped name does not come back after a restart.
  std::string list;
  for (auto &u : users) list += (list.empty() ? "" : ",") + u;
  if (list != savedUsers) {
    savedUsers = list;
    platformSaveSetting("github_users", list);
  }

  // The name and counts are a bonus: failing to get them is not an error.
  JsonDocument filter, doc;
  filter["name"] = true;
  filter["public_repos"] = true;
  filter["followers"] = true;
  if (platformJsonGet("https://api.github.com/users/" + user(), doc, filter) == 200) {
    std::string name;
    for (const char *s = doc["name"] | ""; *s; s++) {
      if (*s >= 32 && *s < 127) name += *s;  // the fonts only cover ASCII
    }
    profileLine = name.empty() ? "" : name + " - ";
    profileLine += std::to_string(doc["public_repos"] | 0) + " repos, " +
                   std::to_string(doc["followers"] | 0) + " followers";
  }

  statusIsError = false;
  statusLine = time(nullptr) > 1600000000 ? "Updated " + clockText() + " - tap to refresh"
                                          : "Tap to refresh";
  printf("GitHub: loaded %d days for %s, total %s\n", (int)reader.days.size(), user().c_str(),
         totalText.c_str());
  drawCalendar();
}

// ---------- choosing a user ----------

static const int CHOICE_TOP = TITLE_H + 34;
static const int CHOICE_H = 36;
static const int INPUT_H = 40;
static const int INPUT_TOP = keyboard::TOP - INPUT_H;

static void loadUsers() {
  if (!users.empty()) return;
  std::string saved = platformLoadSetting("github_users");
  savedUsers = saved;
  size_t at = 0;
  while (at < saved.size() && users.size() < MAX_USERS) {
    size_t comma = saved.find(',', at);
    if (comma == std::string::npos) comma = saved.size();
    if (comma > at) users.push_back(saved.substr(at, comma - at));
    at = comma + 1;
  }
  if (users.empty()) users.push_back(GITHUB_USER);
}

static void drawInput() {
  lcd.startWrite();
  lcd.fillRect(0, INPUT_TOP, lcd.width(), INPUT_H, colour::background);
  lcd.fillSmoothRoundRect(6, INPUT_TOP + 4, lcd.width() - 12, INPUT_H - 8, 10, colour::bar);
  int cy = INPUT_TOP + INPUT_H / 2;
  lcd.setTextDatum(middle_left);
  lcd.setTextColor(colour::dim);
  lcd.drawString("@", 16, cy, &fonts::DejaVu18);
  int x = 16 + lcd.textWidth("@", &fonts::DejaVu18) + 2;
  if (draft.empty()) {
    lcd.drawString("username", x + 4, cy, &fonts::DejaVu18);
  } else {
    lcd.setTextColor(colour::text);
    lcd.drawString(draft.c_str(), x, cy, &fonts::DejaVu18);
    x += lcd.textWidth(draft.c_str(), &fonts::DejaVu18) + 2;
  }
  lcd.fillRect(x, cy - 10, 2, 20, colour::green);
  lcd.endWrite();
}

static void drawChooser() {
  lcd.startWrite();
  lcd.fillRect(0, TITLE_H, lcd.width(), lcd.height() - TITLE_H, colour::background);
  lcd.setTextDatum(top_left);
  lcd.setTextColor(colour::dim);
  lcd.drawString("Pick a recent user or type a username", MARGIN, TITLE_H + 12, FONT_BODY);

  for (size_t i = 0; i < users.size(); i++) {
    int y = CHOICE_TOP + (int)i * CHOICE_H;
    lcd.fillSmoothRoundRect(6, y + 2, lcd.width() - 12, CHOICE_H - 4, 10, colour::bar);
    lcd.setTextDatum(middle_left);
    lcd.setTextColor(colour::text);
    lcd.drawString(fit("@" + users[i], lcd.width() - 90, &fonts::DejaVu18).c_str(), 18,
                   y + CHOICE_H / 2, &fonts::DejaVu18);
    if (i == 0) {
      lcd.setTextDatum(middle_right);
      lcd.setTextColor(colour::green);
      lcd.drawString("showing", lcd.width() - 18, y + CHOICE_H / 2, FONT_BODY);
    }
  }
  lcd.endWrite();
  drawInput();
  keyboard::open("Show", false);
}

// Makes a user the one on show, remembers them, and loads their calendar.
static void choose(const std::string &name) {
  choosing = false;
  if (name != user()) {
    for (size_t i = 0; i < users.size(); i++) {
      if (users[i] == name) {
        users.erase(users.begin() + i);
        break;
      }
    }
    users.insert(users.begin(), name);
    if (users.size() > MAX_USERS) users.pop_back();

    loaded = false;
    profileLine.clear();
    statusLine.clear();
    statusIsError = false;
    refreshWanted = true;
    printf("GitHub: now showing %s\n", name.c_str());
  }
  drawCalendar();
}

static void updateChooser(const Touch &touch) {
  if (touch.pressed && touch.y >= CHOICE_TOP && touch.y < INPUT_TOP) {
    size_t index = (touch.y - CHOICE_TOP) / CHOICE_H;
    if (index < users.size()) choose(std::string(users[index]));
    return;
  }

  char typed = 0;
  switch (keyboard::update(touch, &typed)) {
    case keyboard::TYPED: {
      // GitHub usernames are letters, digits and hyphens only.
      bool allowed = isalnum((unsigned char)typed) || (typed == '-' && !draft.empty());
      if (allowed && draft.size() < MAX_NAME) draft += typed;
      drawInput();
      break;
    }
    case keyboard::BACKSPACE:
      if (!draft.empty()) draft.pop_back();
      drawInput();
      break;
    case keyboard::ACTION:
      if (!draft.empty()) choose(draft);
      break;
    case keyboard::NONE:
      break;
  }
}

// ---------- app ----------

static void enter() {
  loadUsers();
  choosing = false;
  drawCalendar();
  // Refresh on opening, unless what is on show is recent enough.
  bool stale = lgfx::millis() - lastAttempt > REFRESH_MINUTES * 60000UL;
  refreshWanted = !loaded || statusIsError || stale;
}

static void update(const Touch &touch) {
  if (choosing) {
    updateChooser(touch);
    return;
  }
  if (touch.pressed && touch.y > TITLE_H && touch.y < HEADER_BOTTOM) {
    choosing = true;
    draft.clear();
    drawChooser();
    return;
  }

  uint32_t sinceAttempt = lgfx::millis() - lastAttempt;
  bool tapped = touch.pressed && touch.y >= HEADER_BOTTOM && sinceAttempt > 3000;
  // Retry sooner after a failure than after a successful update.
  uint32_t interval = statusIsError ? 30000UL : REFRESH_MINUTES * 60000UL;
  if (refreshWanted || tapped || sinceAttempt > interval) refresh();
}

const App githubApp = {"GitHub", drawGithubIcon, enter, update};

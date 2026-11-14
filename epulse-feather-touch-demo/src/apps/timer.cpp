// Timer: a countdown timer and a stopwatch, on two tabs. Both keep running
// while other apps are open. The device has no speaker, so when the countdown
// ends the Timer opens itself and the screen flashes until it is tapped.

#include <cstdio>
#include <string>
#include <vector>

#include "apps.h"

// ---------- layout ----------

static const int TABS_TOP = TITLE_H + 8;
static const int TABS_H = 32;
static const int TIME_TOP = TABS_TOP + TABS_H + 8;
static const int TIME_H = 92;
static const int BELOW_TIME = TIME_TOP + TIME_H;
static const int PILL_H = 34;
static const int PRESETS_TOP = BELOW_TIME + 8;
static const int ADJUST_TOP = PRESETS_TOP + PILL_H + 8;
static const int TIMER_BUTTONS_TOP = ADJUST_TOP + PILL_H + 16;
static const int WATCH_BUTTONS_TOP = BELOW_TIME + 8;
static const int BUTTON_H = 54;
static const int LAPS_TOP = WATCH_BUTTONS_TOP + BUTTON_H + 18;
static const int LAP_H = 26;
static const int LAPS_SHOWN = 7;

static const lgfx::IFont *const FONT_BODY = &fonts::DejaVu12;
static const lgfx::IFont *const FONT_TIME = &fonts::DejaVu56;

static const uint32_t COLOUR_GO = 0x1F7A37u;
static const uint32_t COLOUR_STOP = 0xB3261Eu;

// ---------- state ----------

enum Tab { TIMER, STOPWATCH };
static Tab tab = TIMER;
static bool isOpen = false;

// The countdown. While running, `timerEnds` is the clock reading at which it
// reaches zero; otherwise `timerLeft` holds what remains.
enum TimerState { READY, RUNNING, PAUSED, FINISHED };
static TimerState timerState = READY;
static uint32_t timerLength = 5 * 60000UL;  // what Reset goes back to
static uint32_t timerLeft = 5 * 60000UL;
static uint32_t timerEnds = 0;
static const uint32_t TIMER_MAX = 99 * 60000UL + 59000UL;

// The stopwatch. While running, the time is `watchBanked` plus however long
// it has been since `watchStarted`.
static bool watchRunning = false;
static uint32_t watchBanked = 0;
static uint32_t watchStarted = 0;
static std::vector<uint32_t> laps;  // the stopwatch reading at each lap
static const size_t MAX_LAPS = 99;

static std::string shownTime;  // what is on the screen, to redraw only changes
static bool flashOn = false;

// ---------- time ----------

static uint32_t timerRemaining() {
  if (timerState != RUNNING) return timerState == FINISHED ? 0 : timerLeft;
  // Subtracting clock readings stays correct even when the clock wraps round.
  int32_t left = (int32_t)(timerEnds - lgfx::millis());
  return left > 0 ? (uint32_t)left : 0;
}

static uint32_t watchElapsed() {
  return watchBanked + (watchRunning ? lgfx::millis() - watchStarted : 0);
}

// "05:00" for the countdown, rounded up so it reads 00:01 until it is over.
static std::string countdownText(uint32_t ms) {
  uint32_t seconds = (ms + 999) / 1000;
  char buf[16];
  snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)(seconds / 60), (unsigned)(seconds % 60));
  return buf;
}

// "01:23.4" for the stopwatch, to a tenth of a second.
static std::string watchText(uint32_t ms) {
  char buf[20];
  snprintf(buf, sizeof(buf), "%02u:%02u.%u", (unsigned)(ms / 60000), (unsigned)(ms / 1000 % 60),
           (unsigned)(ms / 100 % 10));
  return buf;
}

// ---------- drawing ----------

static void drawPill(int x, int y, int w, int h, const char *label, uint32_t fill,
                     const lgfx::IFont *font = FONT_BODY) {
  lcd.fillSmoothRoundRect(x, y, w, h, h / 2 > 14 ? 14 : h / 2, fill);
  lcd.setTextDatum(middle_center);
  lcd.setTextColor(colour::text);
  lcd.drawString(label, x + w / 2, y + h / 2, font);
}

static void drawTabs() {
  const int w = (SCREEN_W - 16) / 2;
  drawPill(8, TABS_TOP, w, TABS_H, "Timer", tab == TIMER ? colour::rule : colour::bar);
  drawPill(8 + w, TABS_TOP, w, TABS_H, "Stopwatch", tab == STOPWATCH ? colour::rule : colour::bar);
}

// Draws the big time. Each character has a fixed place, so a changing digit
// simply overwrites the one before it and nothing flickers.
static void drawTime(bool force) {
  bool finished = tab == TIMER && timerState == FINISHED;
  std::string text = tab == TIMER ? countdownText(timerRemaining()) : watchText(watchElapsed());
  std::string key = text + (finished ? (flashOn ? "!" : "?") : "");
  if (!force && key == shownTime) return;
  bool redrawAll = force || finished || shownTime.size() != key.size();
  shownTime = key;

  uint32_t background = finished && flashOn ? COLOUR_STOP : colour::background;
  lcd.startWrite();
  if (redrawAll) lcd.fillRect(0, TIME_TOP, lcd.width(), TIME_H, background);

  int digitW = lcd.textWidth("0", FONT_TIME), markW = lcd.textWidth(":", FONT_TIME) + 4;
  int total = 0;
  for (char c : text) total += isdigit((unsigned char)c) ? digitW : markW;
  int x = (lcd.width() - total) / 2;
  int y = TIME_TOP + (finished ? 8 : 18);

  lcd.setTextDatum(top_center);
  lcd.setTextColor(colour::text, background);
  for (char c : text) {
    int w = isdigit((unsigned char)c) ? digitW : markW;
    char one[2] = {c, 0};
    lcd.drawString(one, x + w / 2, y, FONT_TIME);
    x += w;
  }
  if (finished) {
    lcd.setTextColor(colour::text);
    lcd.drawString("Time's up - tap to stop", lcd.width() / 2, TIME_TOP + TIME_H - 22, FONT_BODY);
  }
  lcd.endWrite();
}

static void drawTimerControls() {
  lcd.fillRect(0, BELOW_TIME, lcd.width(), lcd.height() - BELOW_TIME, colour::background);

  static const char *const PRESETS[] = {"1 min", "3 min", "5 min", "10 min"};
  static const char *const ADJUST[] = {"- 1 min", "- 10 s", "+ 10 s", "+ 1 min"};
  const int w = 72, gap = 5;
  for (int i = 0; i < 4; i++) {
    int x = 8 + i * (w + gap);
    drawPill(x, PRESETS_TOP, w, PILL_H, PRESETS[i], colour::rule);
    drawPill(x, ADJUST_TOP, w, PILL_H, ADJUST[i], colour::bar);
  }

  const int half = (SCREEN_W - 24) / 2;
  bool running = timerState == RUNNING;
  drawPill(8, TIMER_BUTTONS_TOP, half, BUTTON_H,
           running ? "Pause" : timerState == PAUSED ? "Resume" : "Start",
           running ? COLOUR_STOP : COLOUR_GO, &fonts::DejaVu18);
  drawPill(16 + half, TIMER_BUTTONS_TOP, half, BUTTON_H, "Reset", colour::rule, &fonts::DejaVu18);

  lcd.setTextDatum(top_center);
  lcd.setTextColor(colour::dim);
  const char *note = running                   ? "Running, even while other apps are open."
                     : timerState == PAUSED    ? "Paused."
                     : timerState == FINISHED  ? ""
                                               : "Pick a time, then Start.";
  lcd.drawString(note, lcd.width() / 2, TIMER_BUTTONS_TOP + BUTTON_H + 16, FONT_BODY);
}

static void drawLaps() {
  lcd.fillRect(0, LAPS_TOP - 4, lcd.width(), lcd.height() - LAPS_TOP + 4, colour::background);
  if (laps.empty()) {
    lcd.setTextDatum(top_center);
    lcd.setTextColor(colour::dim);
    lcd.drawString(watchRunning ? "Tap Lap to record a lap time." : "", lcd.width() / 2, LAPS_TOP,
                   FONT_BODY);
    return;
  }
  // Newest lap first, as many as fit.
  for (int row = 0; row < LAPS_SHOWN && row < (int)laps.size(); row++) {
    int index = (int)laps.size() - 1 - row;
    int y = LAPS_TOP + row * LAP_H;
    uint32_t since = laps[index] - (index > 0 ? laps[index - 1] : 0);
    lcd.setTextDatum(top_left);
    lcd.setTextColor(colour::dim);
    lcd.drawString(("Lap " + std::to_string(index + 1)).c_str(), 16, y, FONT_BODY);
    lcd.setTextDatum(top_right);
    lcd.setTextColor(colour::text);
    lcd.drawString(watchText(since).c_str(), 196, y, FONT_BODY);  // this lap alone
    lcd.setTextColor(colour::dim);
    lcd.drawString(watchText(laps[index]).c_str(), lcd.width() - 16, y, FONT_BODY);  // running total
    lcd.drawFastHLine(12, y + LAP_H - 6, lcd.width() - 24, colour::bar);
  }
}

static void drawWatchControls() {
  lcd.fillRect(0, BELOW_TIME, lcd.width(), lcd.height() - BELOW_TIME, colour::background);
  const int half = (SCREEN_W - 24) / 2;
  drawPill(8, WATCH_BUTTONS_TOP, half, BUTTON_H, watchRunning ? "Stop" : "Start",
           watchRunning ? COLOUR_STOP : COLOUR_GO, &fonts::DejaVu18);
  drawPill(16 + half, WATCH_BUTTONS_TOP, half, BUTTON_H, watchRunning ? "Lap" : "Reset",
           colour::rule, &fonts::DejaVu18);
  drawLaps();
}

static void drawControls() {
  lcd.startWrite();
  if (tab == TIMER) {
    drawTimerControls();
  } else {
    drawWatchControls();
  }
  lcd.endWrite();
}

static void drawAll() {
  lcd.startWrite();
  lcd.fillRect(0, TITLE_H, lcd.width(), lcd.height() - TITLE_H, colour::background);
  drawTabs();
  drawTime(true);
  drawControls();
  lcd.endWrite();
}

// ---------- the countdown ----------

static void setTimer(uint32_t ms) {
  if (ms < 10000) ms = 10000;  // ten seconds is the shortest
  if (ms > TIMER_MAX) ms = TIMER_MAX;
  timerLength = timerLeft = ms;
  timerState = READY;
}

static void tapTimer(const Touch &touch) {
  if (timerState == FINISHED) {  // any tap stops the flashing
    printf("Timer: alarm stopped\n");
    setTimer(timerLength);
    drawAll();
    return;
  }

  const int w = 72, gap = 5;
  int column = (touch.x - 8) / (w + gap);
  if (column < 0) column = 0;
  if (column > 3) column = 3;

  if (touch.y >= PRESETS_TOP && touch.y < PRESETS_TOP + PILL_H) {
    static const uint32_t PRESETS[] = {1, 3, 5, 10};
    setTimer(PRESETS[column] * 60000UL);
  } else if (touch.y >= ADJUST_TOP && touch.y < ADJUST_TOP + PILL_H) {
    // Adjusting changes the time being set, or adds to one that is running.
    static const int32_t STEPS[] = {-60000, -10000, 10000, 60000};
    uint32_t left = timerRemaining();
    int32_t changed = (int32_t)left + STEPS[column];
    if (timerState == RUNNING) {
      if (changed < 1000) changed = 1000;
      if (changed > (int32_t)TIMER_MAX) changed = TIMER_MAX;
      timerEnds = lgfx::millis() + (uint32_t)changed;
    } else {
      TimerState was = timerState;
      setTimer(changed > 0 ? (uint32_t)changed : 0);
      if (was == PAUSED) timerState = PAUSED;
    }
  } else if (touch.y >= TIMER_BUTTONS_TOP && touch.y < TIMER_BUTTONS_TOP + BUTTON_H) {
    if (touch.x >= lcd.width() / 2) {
      setTimer(timerLength);  // Reset
    } else if (timerState == RUNNING) {
      timerLeft = timerRemaining();
      timerState = PAUSED;
    } else {
      timerEnds = lgfx::millis() + timerLeft;
      timerState = RUNNING;
      printf("Timer: started, %u seconds\n", (unsigned)(timerLeft / 1000));
    }
  } else {
    return;
  }
  drawTime(true);
  drawControls();
}

// ---------- the stopwatch ----------

static void tapWatch(const Touch &touch) {
  if (touch.y < WATCH_BUTTONS_TOP || touch.y >= WATCH_BUTTONS_TOP + BUTTON_H) return;
  if (touch.x < lcd.width() / 2) {
    if (watchRunning) {
      watchBanked = watchElapsed();
      watchRunning = false;
    } else {
      watchStarted = lgfx::millis();
      watchRunning = true;
    }
  } else if (watchRunning) {
    if (laps.size() < MAX_LAPS) laps.push_back(watchElapsed());
    lcd.startWrite();
    drawLaps();
    lcd.endWrite();
    return;
  } else {
    watchBanked = 0;
    laps.clear();
  }
  drawTime(true);
  drawControls();
}

// ---------- app ----------

static void enter() {
  isOpen = true;
  if (timerState == FINISHED) tab = TIMER;  // show the alarm
  drawAll();
}

static void leave() { isOpen = false; }

// Runs all the time, so the countdown is noticed even from another app.
static void background() {
  if (timerState == RUNNING && timerRemaining() == 0) {
    timerState = FINISHED;
    printf("Timer: time's up\n");
    if (isOpen) {
      tab = TIMER;
      drawAll();
    } else {
      shellOpen(timerApp);  // come to the front, since there is no sound to make
    }
  }
}

// A marker on the logo while a countdown is running or has finished.
static int badge() { return timerState == RUNNING || timerState == FINISHED ? 1 : 0; }

static void update(const Touch &touch) {
  if (touch.pressed) {
    if (touch.y >= TABS_TOP && touch.y < TABS_TOP + TABS_H && timerState != FINISHED) {
      Tab chosen = touch.x < lcd.width() / 2 ? TIMER : STOPWATCH;
      if (chosen != tab) {
        tab = chosen;
        drawAll();
      }
      return;
    }
    if (tab == TIMER) {
      tapTimer(touch);
    } else {
      tapWatch(touch);
    }
    return;
  }

  // While the alarm is going, the time panel flashes red twice a second.
  if (tab == TIMER && timerState == FINISHED) flashOn = lgfx::millis() / 400 % 2 == 0;
  drawTime(false);
}

const App timerApp = {"Timer", drawTimerIcon, enter, update, leave, background, badge};

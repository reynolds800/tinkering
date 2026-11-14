// The shell: draws the home screen, opens apps and brings you back when the
// back arrow in an app's title bar is tapped. Shared by the device and the
// simulator, so nothing here may be Arduino- or ESP32-specific.

#include <cstdio>

#include "app.h"
#include "apps.h"
#include "platform.h"

// ---------- home screen ----------

// A 3 x 4 grid of app slots under a strip showing the time.
static const int COLS = 3;
static const int ROWS = 4;
static const int HOME_TOP = 40;
static const int CELL_W = 106;
static const int CELL_H = 110;
static const int GRID_LEFT = (SCREEN_W - COLS * CELL_W) / 2;

static const App *const slots[COLS * ROWS] = {&calculatorApp, &messagesApp, &drawingApp,
                                              &githubApp,     &weatherApp,  &calendarApp,
                                              &worldClockApp};

static const App *current = nullptr;  // the open app, or nullptr on the home screen
static std::string shownClock;
static int shownBadges = 0;

static int iconX(int slot) { return GRID_LEFT + (slot % COLS) * CELL_W + (CELL_W - ICON_SIZE) / 2; }
static int iconY(int slot) { return HOME_TOP + (slot / COLS) * CELL_H + 6; }

static void drawHomeClock() {
  lcd.fillRect(0, 0, lcd.width(), HOME_TOP, colour::background);
  lcd.setTextDatum(middle_center);
  lcd.setTextColor(colour::text);
  lcd.drawString(clockText().c_str(), lcd.width() / 2, HOME_TOP / 2 + 2, &fonts::DejaVu18);
}

static void drawHome() {
  lcd.startWrite();
  lcd.fillScreen(colour::background);
  drawHomeClock();
  for (int i = 0; i < COLS * ROWS; i++) {
    int x = iconX(i), y = iconY(i);
    if (slots[i]) {
      slots[i]->drawIcon(x, y);
      lcd.setTextDatum(top_center);
      lcd.setTextColor(colour::text);
      lcd.drawString(slots[i]->name, x + ICON_SIZE / 2, y + ICON_SIZE + 6, &fonts::DejaVu12);
      int count = slots[i]->badge ? slots[i]->badge() : 0;
      if (count > 0) {  // red counter on the logo's top-right corner
        int bx = x + ICON_SIZE - 6, by = y + 6;
        lcd.fillSmoothCircle(bx, by, 11, colour::red);
        lcd.setTextDatum(middle_center);
        lcd.drawString(count > 9 ? "9+" : std::to_string(count).c_str(), bx, by + 1,
                       &fonts::DejaVu12);
      }
    } else {
      lcd.drawRoundRect(x, y, ICON_SIZE, ICON_SIZE, 18, colour::bar);  // empty slot
    }
  }
  lcd.endWrite();
}

static void open(const App *app) {
  printf("Opening %s\n", app->name);
  current = app;
  lcd.startWrite();
  lcd.fillScreen(colour::background);
  drawTitleBar(app->name);
  app->enter();
  lcd.endWrite();
}

// Adds up every app's badge, to notice when the home screen needs redrawing.
static int totalBadges() {
  int total = 0;
  for (const App *app : slots) {
    if (app && app->badge) total += app->badge();
  }
  return total;
}

static void goHome() {
  printf("Back to home\n");
  if (current->leave) current->leave();
  current = nullptr;
  shownBadges = totalBadges();
  drawHome();
}

// ---------- main loop ----------

static void step(const Touch &touch) {
  for (const App *app : slots) {
    if (app && app->background) app->background();
  }
  if (!current && totalBadges() != shownBadges) {
    shownBadges = totalBadges();
    drawHome();
  }

  std::string now = clockText();
  if (now != shownClock) {
    shownClock = now;
    if (current) {
      drawTitleClock();
    } else {
      drawHomeClock();
    }
  }

  if (!current) {
    if (!touch.pressed) return;
    for (int i = 0; i < COLS * ROWS; i++) {
      // The label under the icon counts as part of the target.
      if (slots[i] && inside(touch, iconX(i) - 6, iconY(i) - 4, ICON_SIZE + 12, ICON_SIZE + 28)) {
        open(slots[i]);
        return;
      }
    }
    return;
  }

  if (touch.pressed && inside(touch, 0, 0, 110, TITLE_H + 4)) {
    goHome();
    return;
  }
  current->update(touch);
}

void appSetup() {
  printf("Starting\n");
  lcd.init();
  lcd.setRotation(SCREEN_ROTATION);
  lcd.setBrightness(200);
  platformBegin();
  shownClock = clockText();
  drawHome();
}

void appLoop() {
  static Touch touch;
  int32_t x = 0, y = 0;
  bool down = lcd.getTouch(&x, &y) > 0;
  touch.pressed = down && !touch.down;
  touch.released = !down && touch.down;
  touch.down = down;
  if (down) {
    touch.x = x;
    touch.y = y;
  }
  step(touch);
  lgfx::delay(10);
}

void appDrag(int x0, int y0, int x1, int y1) {
  Touch touch;
  touch.x = x0;
  touch.y = y0;
  touch.down = touch.pressed = true;
  step(touch);
  touch.pressed = false;
  const int steps = 12;
  for (int i = 1; i <= steps; i++) {
    touch.x = x0 + (x1 - x0) * i / steps;
    touch.y = y0 + (y1 - y0) * i / steps;
    step(touch);
  }
  touch.down = false;
  touch.released = true;
  step(touch);
}

void appTap(int x, int y) {
  Touch touch;
  touch.x = x;
  touch.y = y;
  touch.down = touch.pressed = true;
  step(touch);
  touch.down = touch.pressed = false;
  touch.released = true;
  step(touch);
}

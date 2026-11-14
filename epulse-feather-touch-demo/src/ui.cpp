#include "ui.h"

#include <cstdio>
#include <ctime>

Display lcd;

std::string clockText() {
  time_t now = time(nullptr);
  if (now < 1600000000) return "--:--";  // clock not set yet
  struct tm local;
  localtime_r(&now, &local);
  char buf[8];
  snprintf(buf, sizeof(buf), "%d:%02d", local.tm_hour, local.tm_min);
  return buf;
}

void drawTitleClock() {
  const int w = 70;
  lcd.fillRect(lcd.width() - w, 0, w, TITLE_H - 1, colour::bar);
  lcd.setTextDatum(middle_right);
  lcd.setTextColor(colour::text);
  lcd.drawString(clockText().c_str(), lcd.width() - 10, TITLE_H / 2, &fonts::DejaVu18);
}

void drawTitleBar(const char *title) {
  lcd.fillRect(0, 0, lcd.width(), TITLE_H - 1, colour::bar);
  lcd.drawFastHLine(0, TITLE_H - 1, lcd.width(), colour::rule);

  // Back arrow.
  int cy = TITLE_H / 2;
  lcd.drawWideLine(20, cy - 8, 12, cy, 1.2f, colour::orange);
  lcd.drawWideLine(12, cy, 20, cy + 8, 1.2f, colour::orange);

  lcd.setTextDatum(middle_left);
  lcd.setTextColor(colour::text);
  lcd.drawString(title, 36, cy, &fonts::DejaVu18);
  drawTitleClock();
}

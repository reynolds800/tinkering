// Things every screen shares: the display, colours, touch input and the title bar.
#pragma once

#include <string>

#include "display.h"

extern Display lcd;

namespace colour {
constexpr uint32_t background = 0x000000u;
constexpr uint32_t bar = 0x1C1C1Eu;
constexpr uint32_t rule = 0x3A3A3Cu;
constexpr uint32_t text = 0xFFFFFFu;
constexpr uint32_t dim = 0x8E8E93u;
constexpr uint32_t orange = 0xFF9500u;
constexpr uint32_t green = 0x34C759u;
constexpr uint32_t red = 0xFF453Au;
}  // namespace colour

struct Touch {
  bool down = false;      // a finger is on the screen
  bool pressed = false;   // it went down this instant
  bool released = false;  // it lifted this instant
  int x = 0, y = 0;       // where it is, or was last
};

inline bool inside(const Touch &t, int x, int y, int w, int h) {
  return t.x >= x && t.x < x + w && t.y >= y && t.y < y + h;
}

// Every app has a title bar: back arrow, app name and the clock.
static const int TITLE_H = 36;
void drawTitleBar(const char *title);
void drawTitleClock();

// The time as "8:42", or "--:--" until the clock has been set.
std::string clockText();

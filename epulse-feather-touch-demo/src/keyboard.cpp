#include "keyboard.h"

#include <string>
#include <vector>

namespace keyboard {

static const uint32_t COLOUR_KEY = 0x3A3A3Cu;
static const uint32_t COLOUR_KEY_SPECIAL = 0x232325u;
static const uint32_t COLOUR_KEY_HELD = 0x8E8E93u;
static const uint32_t COLOUR_ACTION = 0x1F7A37u;

enum : char { KEY_SHIFT = 1, KEY_BACKSPACE = 2, KEY_PAGE = 3, KEY_ACTION = 4 };

struct Key {
  int x, y, w;
  char code;  // the character typed, or one of the KEY_ values above
};

static std::vector<Key> keys;
static std::string actionLabel;
static bool symbolPage = false;
static bool shifted = false;
static int heldKey = -1;

static void layout() {
  const char *row1 = symbolPage ? "1234567890" : "qwertyuiop";
  const char *row2 = symbolPage ? "-/:;()$&@" : "asdfghjkl";
  const char *row3 = symbolPage ? ".,?!'\"+" : "zxcvbnm";
  keys.clear();
  for (int i = 0; i < 10; i++) keys.push_back({i * 32, TOP, 32, row1[i]});
  for (int i = 0; i < 9; i++) keys.push_back({16 + i * 32, TOP + KEY_H, 32, row2[i]});
  int y3 = TOP + 2 * KEY_H;
  keys.push_back({0, y3, 48, KEY_SHIFT});
  for (int i = 0; i < 7; i++) keys.push_back({48 + i * 32, y3, 32, row3[i]});
  keys.push_back({272, y3, 48, KEY_BACKSPACE});
  int y4 = TOP + 3 * KEY_H;
  keys.push_back({0, y4, 64, KEY_PAGE});
  keys.push_back({64, y4, 160, ' '});
  keys.push_back({224, y4, 96, KEY_ACTION});
}

static bool isLetter(char c) { return c >= 'a' && c <= 'z'; }

static void drawKey(int index, bool held) {
  const Key &k = keys[index];
  bool special = k.code == KEY_SHIFT || k.code == KEY_BACKSPACE || k.code == KEY_PAGE;
  uint32_t fill = k.code == KEY_ACTION ? COLOUR_ACTION : special ? COLOUR_KEY_SPECIAL : COLOUR_KEY;
  if (k.code == KEY_SHIFT && shifted && !symbolPage) fill = COLOUR_KEY;
  if (held) fill = COLOUR_KEY_HELD;

  int cx = k.x + k.w / 2, cy = k.y + KEY_H / 2;
  lcd.startWrite();
  lcd.fillRect(k.x, k.y, k.w, KEY_H, colour::background);
  lcd.fillSmoothRoundRect(k.x + 2, k.y + 3, k.w - 4, KEY_H - 6, 6, fill);
  lcd.setTextDatum(middle_center);
  lcd.setTextColor(colour::text);

  if (k.code == KEY_SHIFT) {
    if (!symbolPage) {  // an upward arrow; shift does nothing on the symbols page
      lcd.fillTriangle(cx, cy - 9, cx - 9, cy + 1, cx + 9, cy + 1, colour::text);
      lcd.fillRect(cx - 4, cy + 1, 9, 7, colour::text);
    }
  } else if (k.code == KEY_BACKSPACE) {
    lcd.drawWideLine(cx - 12, cy, cx - 5, cy - 8, 1.0f, colour::text);
    lcd.drawWideLine(cx - 5, cy - 8, cx + 11, cy - 8, 1.0f, colour::text);
    lcd.drawWideLine(cx + 11, cy - 8, cx + 11, cy + 8, 1.0f, colour::text);
    lcd.drawWideLine(cx + 11, cy + 8, cx - 5, cy + 8, 1.0f, colour::text);
    lcd.drawWideLine(cx - 5, cy + 8, cx - 12, cy, 1.0f, colour::text);
    lcd.drawWideLine(cx, cy - 3, cx + 6, cy + 3, 1.0f, colour::text);
    lcd.drawWideLine(cx, cy + 3, cx + 6, cy - 3, 1.0f, colour::text);
  } else if (k.code == KEY_PAGE) {
    lcd.drawString(symbolPage ? "ABC" : "123", cx, cy, &fonts::DejaVu12);
  } else if (k.code == KEY_ACTION) {
    lcd.drawString(actionLabel.c_str(), cx, cy, &fonts::DejaVu12);
  } else if (k.code != ' ') {
    char text[2] = {k.code, 0};
    if (shifted && isLetter(k.code)) text[0] = (char)(k.code - 'a' + 'A');
    lcd.drawString(text, cx, cy, &fonts::DejaVu18);
  }
  lcd.endWrite();
}

static void drawAll() {
  heldKey = -1;  // the key under the finger may have changed
  lcd.startWrite();
  for (int i = 0; i < (int)keys.size(); i++) drawKey(i, false);
  lcd.endWrite();
}

void open(const char *label, bool capitalise) {
  actionLabel = label;
  symbolPage = false;
  shifted = capitalise;
  layout();
  drawAll();
}

void setShift(bool on) {
  if (shifted == on) return;
  shifted = on;
  if (!symbolPage) drawAll();
}

Event update(const Touch &touch, char *typed) {
  Event event = NONE;
  if (touch.pressed && touch.y >= TOP) {
    for (int i = 0; i < (int)keys.size(); i++) {
      if (!inside(touch, keys[i].x, keys[i].y, keys[i].w, KEY_H)) continue;
      heldKey = i;
      drawKey(i, true);
      char code = keys[i].code;
      if (code == KEY_SHIFT) {
        if (!symbolPage) setShift(!shifted);
      } else if (code == KEY_PAGE) {
        symbolPage = !symbolPage;
        layout();
        drawAll();
      } else if (code == KEY_BACKSPACE) {
        event = BACKSPACE;
      } else if (code == KEY_ACTION) {
        event = ACTION;
      } else {
        bool capital = shifted && isLetter(code);
        *typed = capital ? (char)(code - 'a' + 'A') : code;
        event = TYPED;
        if (capital) setShift(false);  // shift applies to one letter
      }
      break;
    }
  }
  if (touch.released && heldKey >= 0) {
    drawKey(heldKey, false);
    heldKey = -1;
  }
  return event;
}

}  // namespace keyboard

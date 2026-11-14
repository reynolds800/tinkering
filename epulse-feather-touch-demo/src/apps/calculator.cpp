// Calculator: add, subtract, multiply, divide, percent and square root, with
// a short "tape" above the number showing the sum being worked out.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "apps.h"

// ---------- layout ----------

static const int DISPLAY_BOTTOM = 180;
static const int KEY_COLS = 4;
static const int KEY_ROWS = 5;
static const int KEY_W = SCREEN_W / KEY_COLS;
static const int KEY_H = (SCREEN_H - DISPLAY_BOTTOM) / KEY_ROWS;

// One character per key: '<' is backspace, 'r' is square root, '*' and '/'
// are drawn as multiply and divide signs.
static const char KEYS[KEY_ROWS * KEY_COLS + 1] =
    "C<%/"
    "789*"
    "456-"
    "123+"
    "r0.=";

static const size_t MAX_DIGITS = 10;

// ---------- state ----------

static std::string entry;    // the number on show
static bool typing;          // entry was typed or produced as an operand, not just a running total
static bool replaceNext;     // the next digit starts a new number
static double total;         // left-hand side of the pending operation
static char pending;         // '+', '-', '*', '/' or 0
static bool failed;          // showing "Error" until cleared
static std::string tape[3];
static int heldKey = -1;

// ---------- arithmetic ----------

static std::string format(double v) {
  if (!std::isfinite(v)) return "Error";
  char buf[32];
  for (int precision = 10; precision >= 1; precision--) {
    snprintf(buf, sizeof(buf), "%.*g", precision, v);
    if (strlen(buf) <= MAX_DIGITS + 1) break;
  }
  std::string s = buf;
  return s == "-0" ? "0" : s;
}

static double value() { return strtod(entry.c_str(), nullptr); }

static double apply(double a, double b, char op) {
  switch (op) {
    case '+': return a + b;
    case '-': return a - b;
    case '*': return a * b;
    case '/': return b == 0 ? NAN : a / b;
  }
  return b;
}

static const char *opText(char op) { return op == '*' ? "x" : op == '/' ? "/" : op == '-' ? "-" : "+"; }

static void clearAll() {
  entry = "0";
  typing = replaceNext = failed = false;
  total = 0;
  pending = 0;
  tape[0] = tape[1] = tape[2] = "";
}

// Shows a calculated number. Returns false if it was an error.
static bool show(double v) {
  entry = format(v);
  failed = entry == "Error";
  if (failed) {
    pending = 0;
    typing = false;
  }
  return !failed;
}

static void pressDigit(char c) {
  if (failed) clearAll();
  if (!typing || replaceNext) {
    if (!pending) tape[0] = tape[1] = tape[2] = "";
    entry = "0";
    typing = true;
    replaceNext = false;
  }
  if (c == '.') {
    if (entry.find('.') == std::string::npos && entry.size() < MAX_DIGITS) entry += '.';
  } else if (entry == "0") {
    entry = c;
  } else if (entry.size() < MAX_DIGITS) {
    entry += c;
  }
}

static void pressOperator(char op) {
  if (failed) return;
  if (pending && typing) {
    // Chained sums such as 2 + 3 + 4 work out the running total as they go.
    if (!show(apply(total, value(), pending))) return;
  }
  total = value();
  pending = op;
  typing = false;
  tape[0] = format(total);
  tape[1] = opText(op);
  tape[2] = "";
}

static void pressEquals() {
  if (failed || !pending) return;
  double right = value();
  tape[0] = format(total);
  tape[1] = std::string(opText(pending)) + " " + format(right);
  if (show(apply(total, right, pending))) tape[2] = "= " + entry;
  pending = 0;
  typing = false;
}

static void pressPercent() {
  if (failed) return;
  // 200 + 10 % means 200 + 10% of 200; otherwise it just divides by 100.
  bool ofTotal = pending == '+' || pending == '-';
  if (show(ofTotal ? total * value() / 100 : value() / 100)) {
    typing = true;
    replaceNext = true;
  }
}

static void pressRoot() {
  if (failed) return;
  double v = value();
  if (!pending) {
    tape[0] = "sqrt(" + format(v) + ")";
    tape[1] = "";
  }
  if (show(v < 0 ? NAN : std::sqrt(v))) {
    if (!pending) tape[2] = "= " + entry;
    typing = true;
    replaceNext = true;
  }
}

static void pressBackspace() {
  if (failed) {
    clearAll();
    return;
  }
  if (!typing || replaceNext) return;  // only typed digits can be deleted
  entry.pop_back();
  if (entry.empty() || entry == "-") entry = "0";
}

static void press(char key) {
  if ((key >= '0' && key <= '9') || key == '.') {
    pressDigit(key);
  } else if (key == '+' || key == '-' || key == '*' || key == '/') {
    pressOperator(key);
  } else if (key == '=') {
    pressEquals();
  } else if (key == '%') {
    pressPercent();
  } else if (key == 'r') {
    pressRoot();
  } else if (key == '<') {
    pressBackspace();
  } else if (key == 'C') {
    clearAll();
  }
}

// ---------- drawing ----------

static void drawDisplay() {
  lcd.startWrite();
  lcd.fillRect(0, TITLE_H, lcd.width(), DISPLAY_BOTTOM - TITLE_H, colour::background);

  const int right = lcd.width() - 14;
  lcd.setTextDatum(top_right);
  lcd.setTextColor(colour::dim);
  for (int i = 0; i < 3; i++) {
    lcd.drawString(tape[i].c_str(), right, TITLE_H + 10 + i * 24, &fonts::DejaVu18);
  }

  // The number shrinks if it would not fit at full size.
  const lgfx::IFont *font = &fonts::DejaVu40;
  if (lcd.textWidth(entry.c_str(), font) > lcd.width() - 28) font = &fonts::DejaVu24;
  lcd.setTextDatum(bottom_right);
  lcd.setTextColor(failed ? colour::red : colour::text);
  lcd.drawString(entry.c_str(), right, DISPLAY_BOTTOM - 8, font);
  lcd.endWrite();
}

// Draws a key's symbol centred on cx, cy.
static void drawSymbol(char key, int cx, int cy, uint32_t ink) {
  switch (key) {
    case '+':
      lcd.fillRect(cx - 9, cy - 1, 19, 3, ink);
      lcd.fillRect(cx - 1, cy - 9, 3, 19, ink);
      break;
    case '-':
      lcd.fillRect(cx - 9, cy - 1, 19, 3, ink);
      break;
    case '*':
      lcd.drawWideLine(cx - 7, cy - 7, cx + 7, cy + 7, 1.4f, ink);
      lcd.drawWideLine(cx - 7, cy + 7, cx + 7, cy - 7, 1.4f, ink);
      break;
    case '/':
      lcd.fillRect(cx - 9, cy - 1, 19, 3, ink);
      lcd.fillSmoothCircle(cx, cy - 7, 2, ink);
      lcd.fillSmoothCircle(cx, cy + 7, 2, ink);
      break;
    case '=':
      lcd.fillRect(cx - 9, cy - 5, 19, 3, ink);
      lcd.fillRect(cx - 9, cy + 3, 19, 3, ink);
      break;
    case 'r':  // square root sign
      lcd.drawWideLine(cx - 12, cy + 1, cx - 7, cy + 9, 1.2f, ink);
      lcd.drawWideLine(cx - 7, cy + 9, cx - 1, cy - 9, 1.2f, ink);
      lcd.drawWideLine(cx - 1, cy - 9, cx + 12, cy - 9, 1.2f, ink);
      break;
    case '<':  // backspace: a left-pointing tag with a cross in it
      lcd.drawWideLine(cx - 14, cy, cx - 6, cy - 9, 1.0f, ink);
      lcd.drawWideLine(cx - 6, cy - 9, cx + 13, cy - 9, 1.0f, ink);
      lcd.drawWideLine(cx + 13, cy - 9, cx + 13, cy + 9, 1.0f, ink);
      lcd.drawWideLine(cx + 13, cy + 9, cx - 6, cy + 9, 1.0f, ink);
      lcd.drawWideLine(cx - 6, cy + 9, cx - 14, cy, 1.0f, ink);
      lcd.drawWideLine(cx, cy - 4, cx + 7, cy + 4, 1.0f, ink);
      lcd.drawWideLine(cx, cy + 4, cx + 7, cy - 4, 1.0f, ink);
      break;
    default: {
      char text[2] = {key, 0};
      lcd.setTextDatum(middle_center);
      lcd.setTextColor(ink);
      lcd.drawString(text, cx, cy, &fonts::DejaVu24);
    }
  }
}

static void drawKey(int index, bool held) {
  char key = KEYS[index];
  bool digit = (key >= '0' && key <= '9') || key == '.';
  bool operation = key == '+' || key == '-' || key == '*' || key == '/' || key == '=';

  uint32_t fill = digit ? 0x333336u : operation ? colour::orange : 0x5A5A5Eu;
  if (held) fill = digit ? 0x6A6A6Eu : operation ? 0xFFC266u : 0x8E8E93u;

  int x = (index % KEY_COLS) * KEY_W;
  int y = DISPLAY_BOTTOM + (index / KEY_COLS) * KEY_H;
  lcd.startWrite();
  lcd.fillSmoothRoundRect(x + 3, y + 3, KEY_W - 6, KEY_H - 6, 10, fill);
  drawSymbol(key, x + KEY_W / 2, y + KEY_H / 2, colour::text);
  lcd.endWrite();
}

// ---------- app ----------

static void enter() {
  clearAll();
  heldKey = -1;
  drawDisplay();
  for (int i = 0; i < KEY_ROWS * KEY_COLS; i++) drawKey(i, false);
}

static void update(const Touch &touch) {
  if (touch.pressed && touch.y >= DISPLAY_BOTTOM) {
    int col = touch.x / KEY_W, row = (touch.y - DISPLAY_BOTTOM) / KEY_H;
    if (col >= 0 && col < KEY_COLS && row >= 0 && row < KEY_ROWS) {
      heldKey = row * KEY_COLS + col;
      drawKey(heldKey, true);
      press(KEYS[heldKey]);
      drawDisplay();
    }
  }
  if (touch.released && heldKey >= 0) {
    drawKey(heldKey, false);
    heldKey = -1;
  }
}

const App calculatorApp = {"Calculator", drawCalculatorIcon, enter, update};

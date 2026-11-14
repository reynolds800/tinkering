// Messages: a chat between everything on the same network that is running
// this software (devices and simulators). Messages are broadcast on the local
// network only, kept in memory, and lost when the device restarts.

#include <ArduinoJson.h>
#include <cstdio>
#include <deque>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "apps.h"
#include "platform.h"

// ---------- layout ----------

static const int KEY_H = 40;
static const int KEYBOARD_TOP = SCREEN_H - 4 * KEY_H;
static const int INPUT_H = 40;
static const int INPUT_TOP = KEYBOARD_TOP - INPUT_H;
static const int CHAT_TOP = TITLE_H;
static const int CHAT_H = INPUT_TOP - CHAT_TOP;

static const int LINE_H = 17;
static const int BUBBLE_MAX_TEXT_W = 210;
static const lgfx::IFont *const CHAT_FONT = &fonts::DejaVu12;
static const lgfx::IFont *const NAME_FONT = &fonts::DejaVu9;

static const uint32_t COLOUR_MINE = 0x1F7A37u;
static const uint32_t COLOUR_THEIRS = 0x2C2C2Eu;
static const uint32_t COLOUR_KEY = 0x3A3A3Cu;
static const uint32_t COLOUR_KEY_SPECIAL = 0x232325u;
static const uint32_t COLOUR_KEY_HELD = 0x8E8E93u;

// ---------- conversation ----------

struct Message {
  std::string from;
  std::string text;
  bool mine;
};

static const size_t MAX_MESSAGES = 30;
static const size_t MAX_TEXT = 120;

static std::deque<Message> messages;
static int unread = 0;
static bool isOpen = false;
static std::string draft;

// Identifies this running copy, so it can ignore its own broadcasts.
static uint32_t instanceId() {
  static uint32_t id = std::random_device{}();
  return id;
}

static std::string printable(const char *s, size_t maxLength) {
  std::string out;
  for (; s && *s && out.size() < maxLength; s++) {
    if (*s >= 32 && *s < 127) out += *s;
  }
  return out;
}

// A message can arrive twice (by broadcast and directly), so recent ones are
// remembered by sender and number.
static bool alreadySeen(uint32_t src, uint32_t id) {
  static std::deque<std::pair<uint32_t, uint32_t>> seen;
  for (auto &entry : seen) {
    if (entry.first == src && entry.second == id) return true;
  }
  seen.emplace_back(src, id);
  if (seen.size() > 32) seen.pop_front();
  return false;
}

static void addMessage(const std::string &from, const std::string &text, bool mine) {
  messages.push_back({from, text, mine});
  if (messages.size() > MAX_MESSAGES) messages.pop_front();
}

// Breaks text into lines no wider than BUBBLE_MAX_TEXT_W.
static std::vector<std::string> wrap(const std::string &text) {
  std::vector<std::string> lines;
  std::string line;
  size_t i = 0;
  while (i < text.size()) {
    size_t end = text.find(' ', i);
    if (end == std::string::npos) end = text.size();
    std::string word = text.substr(i, end - i);
    i = end + 1;

    std::string candidate = line.empty() ? word : line + " " + word;
    if (lcd.textWidth(candidate.c_str(), CHAT_FONT) <= BUBBLE_MAX_TEXT_W) {
      line = candidate;
      continue;
    }
    if (!line.empty()) lines.push_back(line);
    line.clear();
    // A single word too long for a line is split wherever it has to be.
    for (char c : word) {
      if (lcd.textWidth((line + c).c_str(), CHAT_FONT) > BUBBLE_MAX_TEXT_W) {
        lines.push_back(line);
        line.clear();
      }
      line += c;
    }
  }
  if (!line.empty() || lines.empty()) lines.push_back(line);
  return lines;
}

// Draws the newest messages, working upwards from just above the input box.
static void drawChat() {
  lcd.startWrite();
  lcd.fillRect(0, CHAT_TOP, lcd.width(), CHAT_H, colour::background);

  if (messages.empty()) {
    lcd.setTextDatum(middle_center);
    lcd.setTextColor(colour::dim);
    int cy = CHAT_TOP + CHAT_H / 2;
    lcd.drawString("No messages yet", lcd.width() / 2, cy - 20, &fonts::DejaVu18);
    lcd.drawString("Anything you send is seen by other", lcd.width() / 2, cy + 8, CHAT_FONT);
    lcd.drawString("devices on this WiFi running Messages.", lcd.width() / 2, cy + 26, CHAT_FONT);
    lcd.endWrite();
    return;
  }

  lcd.setClipRect(0, CHAT_TOP, lcd.width(), CHAT_H);
  int bottom = INPUT_TOP - 6;
  for (int i = (int)messages.size() - 1; i >= 0 && bottom > CHAT_TOP; i--) {
    const Message &m = messages[i];
    std::vector<std::string> lines = wrap(m.text);
    int textW = 0;
    for (auto &line : lines) {
      int w = lcd.textWidth(line.c_str(), CHAT_FONT);
      if (w > textW) textW = w;
    }
    int w = textW + 22;
    int h = (int)lines.size() * LINE_H + 12;
    int x = m.mine ? lcd.width() - 8 - w : 8;
    int y = bottom - h;

    lcd.fillSmoothRoundRect(x, y, w, h, 12, m.mine ? COLOUR_MINE : COLOUR_THEIRS);
    lcd.setTextDatum(top_left);
    lcd.setTextColor(colour::text);
    for (size_t l = 0; l < lines.size(); l++) {
      lcd.drawString(lines[l].c_str(), x + 11, y + 7 + (int)l * LINE_H, CHAT_FONT);
    }
    bottom = y - 5;

    // The sender's name goes above the first of a run of their messages.
    bool startsRun = i == 0 || messages[i - 1].mine || messages[i - 1].from != m.from;
    if (!m.mine && startsRun) {
      lcd.setTextColor(colour::dim);
      lcd.drawString(m.from.c_str(), x + 6, bottom - 9, NAME_FONT);
      bottom -= 16;
    }
  }
  lcd.clearClipRect();
  lcd.endWrite();
}

static void drawInput() {
  lcd.startWrite();
  lcd.fillRect(0, INPUT_TOP, lcd.width(), INPUT_H, colour::background);
  lcd.fillSmoothRoundRect(6, INPUT_TOP + 4, lcd.width() - 12, INPUT_H - 8, 10, colour::bar);

  // Show the end of the text when it is too long for the box.
  const int maxW = lcd.width() - 44;
  std::string shown = draft;
  while (lcd.textWidth(shown.c_str(), &fonts::DejaVu18) > maxW) shown.erase(0, 1);

  int cy = INPUT_TOP + INPUT_H / 2;
  lcd.setTextDatum(middle_left);
  if (draft.empty()) {
    lcd.setTextColor(colour::dim);
    lcd.drawString("Message", 22, cy, &fonts::DejaVu18);
  } else {
    lcd.setTextColor(colour::text);
    lcd.drawString(shown.c_str(), 18, cy, &fonts::DejaVu18);
  }
  int cursorX = 18 + (draft.empty() ? 0 : lcd.textWidth(shown.c_str(), &fonts::DejaVu18) + 2);
  lcd.fillRect(cursorX, cy - 10, 2, 20, colour::green);
  lcd.endWrite();
}

// ---------- sending and receiving ----------

static void send() {
  // Trim spaces from both ends; an empty message is not sent.
  size_t first = draft.find_first_not_of(' ');
  if (first == std::string::npos) return;
  std::string text = draft.substr(first, draft.find_last_not_of(' ') - first + 1);

  JsonDocument doc;
  doc["app"] = "epulse-messages";
  static uint32_t nextId = 1;
  doc["src"] = instanceId();
  doc["id"] = nextId++;
  doc["from"] = platformDeviceName();
  doc["text"] = text;
  std::string packet;
  serializeJson(doc, packet);
  bool sent = platformChatSend(packet);
  printf("Message %s: %s\n", sent ? "sent" : "not sent (no network)", text.c_str());

  addMessage(platformDeviceName(), sent ? text : text + " (not sent)", true);
  draft.clear();
}

// Collects any messages that have arrived. Runs whether or not the app is open.
static void receive() {
  // Say hello once the network is up, so others learn where to reach us.
  // It has no text, so nobody shows it as a message.
  static bool announced = false;
  if (!announced) {
    JsonDocument hello;
    hello["app"] = "epulse-messages";
    hello["src"] = instanceId();
    std::string out;
    serializeJson(hello, out);
    announced = platformChatSend(out);
  }

  std::string packet;
  bool changed = false;
  while (platformChatReceive(packet)) {
    JsonDocument doc;
    if (deserializeJson(doc, packet)) continue;
    if (std::string(doc["app"] | "") != "epulse-messages") continue;
    uint32_t src = doc["src"] | 0u;
    if (src == instanceId()) continue;  // our own broadcast coming back
    if (alreadySeen(src, doc["id"] | 0u)) continue;
    std::string from = printable(doc["from"] | "", 16);
    std::string text = printable(doc["text"] | "", MAX_TEXT);
    if (text.empty()) continue;
    printf("Message from %s: %s\n", from.c_str(), text.c_str());
    addMessage(from.empty() ? "Someone" : from, text, false);
    changed = true;
    if (!isOpen) unread++;
  }
  if (changed && isOpen) drawChat();
}

static int badge() { return unread; }

// ---------- keyboard ----------

enum : char { KEY_SHIFT = 1, KEY_BACKSPACE = 2, KEY_PAGE = 3, KEY_SEND = 4 };

struct Key {
  int x, y, w;
  char code;  // the character typed, or one of the KEY_ values above
};

static std::vector<Key> keys;
static bool symbolPage = false;
static bool shifted = true;  // start with a capital letter
static int heldKey = -1;

static void layoutKeyboard() {
  const char *row1 = symbolPage ? "1234567890" : "qwertyuiop";
  const char *row2 = symbolPage ? "-/:;()$&@" : "asdfghjkl";
  const char *row3 = symbolPage ? ".,?!'\"+" : "zxcvbnm";
  keys.clear();
  for (int i = 0; i < 10; i++) keys.push_back({i * 32, KEYBOARD_TOP, 32, row1[i]});
  for (int i = 0; i < 9; i++) keys.push_back({16 + i * 32, KEYBOARD_TOP + KEY_H, 32, row2[i]});
  int y3 = KEYBOARD_TOP + 2 * KEY_H;
  keys.push_back({0, y3, 48, KEY_SHIFT});
  for (int i = 0; i < 7; i++) keys.push_back({48 + i * 32, y3, 32, row3[i]});
  keys.push_back({272, y3, 48, KEY_BACKSPACE});
  int y4 = KEYBOARD_TOP + 3 * KEY_H;
  keys.push_back({0, y4, 64, KEY_PAGE});
  keys.push_back({64, y4, 160, ' '});
  keys.push_back({224, y4, 96, KEY_SEND});
}

static bool isLetter(char c) { return c >= 'a' && c <= 'z'; }

static void drawKey(int index, bool held) {
  const Key &k = keys[index];
  bool special = k.code == KEY_SHIFT || k.code == KEY_BACKSPACE || k.code == KEY_PAGE;
  uint32_t fill = k.code == KEY_SEND ? COLOUR_MINE : special ? COLOUR_KEY_SPECIAL : COLOUR_KEY;
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
  } else if (k.code == KEY_SEND) {
    lcd.drawString("Send", cx, cy, &fonts::DejaVu12);
  } else if (k.code != ' ') {
    char text[2] = {k.code, 0};
    if (shifted && isLetter(k.code)) text[0] = (char)(k.code - 'a' + 'A');
    lcd.drawString(text, cx, cy, &fonts::DejaVu18);
  }
  lcd.endWrite();
}

static void drawKeyboard() {
  lcd.startWrite();
  for (int i = 0; i < (int)keys.size(); i++) drawKey(i, false);
  lcd.endWrite();
}

static void pressKey(int index) {
  char code = keys[index].code;
  bool redrawKeyboard = false;

  if (code == KEY_SHIFT) {
    if (symbolPage) return;
    shifted = !shifted;
    redrawKeyboard = true;
  } else if (code == KEY_PAGE) {
    symbolPage = !symbolPage;
    layoutKeyboard();
    redrawKeyboard = true;
  } else if (code == KEY_BACKSPACE) {
    if (!draft.empty()) draft.pop_back();
  } else if (code == KEY_SEND) {
    send();
    drawChat();
    if (!shifted) redrawKeyboard = true;
    shifted = true;  // next message starts with a capital again
  } else if (draft.size() < MAX_TEXT) {
    draft += shifted && isLetter(code) ? (char)(code - 'a' + 'A') : code;
    if (shifted && isLetter(code)) {  // shift applies to one letter
      shifted = false;
      redrawKeyboard = true;
    }
  }

  drawInput();
  if (redrawKeyboard) {
    heldKey = -1;  // the key under the finger may have changed
    drawKeyboard();
  }
}

// ---------- app ----------

static void enter() {
  isOpen = true;
  unread = 0;
  heldKey = -1;
  layoutKeyboard();
  drawChat();
  drawInput();
  drawKeyboard();
}

static void leave() { isOpen = false; }

static void update(const Touch &touch) {
  if (touch.pressed && touch.y >= KEYBOARD_TOP) {
    for (int i = 0; i < (int)keys.size(); i++) {
      if (inside(touch, keys[i].x, keys[i].y, keys[i].w, KEY_H)) {
        heldKey = i;
        drawKey(i, true);
        pressKey(i);
        break;
      }
    }
  }
  if (touch.released && heldKey >= 0) {
    drawKey(heldKey, false);
    heldKey = -1;
  }
}

const App messagesApp = {"Messages", drawMessagesIcon, enter, update, leave, receive, badge};

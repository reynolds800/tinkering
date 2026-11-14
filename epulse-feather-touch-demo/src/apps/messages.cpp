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
#include "keyboard.h"
#include "platform.h"

// ---------- layout ----------

static const int INPUT_H = 40;
static const int INPUT_TOP = keyboard::TOP - INPUT_H;
static const int CHAT_TOP = TITLE_H;
static const int CHAT_H = INPUT_TOP - CHAT_TOP;

static const int LINE_H = 17;
static const int BUBBLE_MAX_TEXT_W = 210;
static const lgfx::IFont *const CHAT_FONT = &fonts::DejaVu12;
static const lgfx::IFont *const NAME_FONT = &fonts::DejaVu9;

static const uint32_t COLOUR_MINE = 0x1F7A37u;
static const uint32_t COLOUR_THEIRS = 0x2C2C2Eu;

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

// ---------- app ----------

static void enter() {
  isOpen = true;
  unread = 0;
  drawChat();
  drawInput();
  keyboard::open("Send", true);  // messages start with a capital letter
}

static void leave() { isOpen = false; }

static void update(const Touch &touch) {
  char typed = 0;
  switch (keyboard::update(touch, &typed)) {
    case keyboard::TYPED:
      if (draft.size() < MAX_TEXT) draft += typed;
      drawInput();
      break;
    case keyboard::BACKSPACE:
      if (!draft.empty()) draft.pop_back();
      drawInput();
      break;
    case keyboard::ACTION:
      send();
      drawChat();
      drawInput();
      keyboard::setShift(true);
      break;
    case keyboard::NONE:
      break;
  }
}

const App messagesApp = {"Messages", drawMessagesIcon, enter, update, leave, receive, badge};

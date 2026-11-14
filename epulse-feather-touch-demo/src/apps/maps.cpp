// Maps: a street map you can drag around, zoom in and out of, and search.
// The map is made of 256-pixel square images ("tiles") downloaded from
// OpenStreetMap as they are needed, and places are found with OpenStreetMap's
// Nominatim search.

#include <ArduinoJson.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "apps.h"
#include "keyboard.h"
#include "platform.h"

// ---------- layout ----------

static const int MARGIN = 12;
static const int BAR_TOP = TITLE_H + 4;
static const int BAR_H = 34;
static const int MAP_TOP = BAR_TOP + BAR_H + 2;
static const int MAP_H = SCREEN_H - MAP_TOP;
static const int BUTTON = 42;  // zoom buttons, bottom right
static const int BUTTON_X = SCREEN_W - BUTTON - 8;
static const int ZOOM_IN_Y = SCREEN_H - 2 * BUTTON - 16;
static const int ZOOM_OUT_Y = SCREEN_H - BUTTON - 10;

static const lgfx::IFont *const FONT_BODY = &fonts::DejaVu12;
static const lgfx::IFont *const FONT_SMALL = &fonts::DejaVu9;

static const uint32_t COLOUR_LAND = 0xE8E4DCu;  // shown where no tile is loaded
static const uint32_t COLOUR_PIN = 0xFF453Au;

static const int TILE = 256;
static const int MIN_ZOOM = 2;
static const int MAX_ZOOM = 19;
static const double PI_ = 3.14159265358979323846;

// ---------- state ----------

static double latitude = -33.4482, longitude = 151.4467;  // centre of the view: Terrigal
static int zoom = 13;
static bool viewLoaded = false;

static bool pinShown = false;
static double pinLatitude = 0, pinLongitude = 0;
static std::string pinName;

// The view is drawn in memory and copied to the screen, so it can be slid
// around under a finger. Without the memory it is drawn straight to the screen.
static LGFX_Sprite view(&lcd);
static bool viewReady = false;

static bool renderWanted = false;
static std::string problem;  // shown over the map when tiles cannot be loaded

static bool dragging = false;
static int dragStartX = 0, dragStartY = 0, dragX = 0, dragY = 0;

// The place search screen.
struct Place {
  std::string name;
  double latitude, longitude;
  double south, north, west, east;  // the area it covers
};
static bool searching = false;
static std::string query;
static std::string searchHint;
static std::vector<Place> choices;

// ---------- map arithmetic ----------

// The whole world is a square of this many pixels at the current zoom.
static double worldSize(int z) { return (double)TILE * (double)(1L << z); }

// Positions on that square, in pixels, for a longitude and latitude
// (the Web Mercator projection that tiles use).
static double xOf(double lon, int z) { return (lon + 180.0) / 360.0 * worldSize(z); }
static double yOf(double lat, int z) {
  double r = lat * PI_ / 180.0;
  return (1.0 - asinh(tan(r)) / PI_) / 2.0 * worldSize(z);
}
static double longitudeOf(double x, int z) { return x / worldSize(z) * 360.0 - 180.0; }
static double latitudeOf(double y, int z) {
  return atan(sinh(PI_ * (1.0 - 2.0 * y / worldSize(z)))) * 180.0 / PI_;
}

// Keeps the centre on the map: tiles stop near 85 degrees north and south,
// and longitude wraps round.
static void tidyCentre() {
  if (latitude > 85) latitude = 85;
  if (latitude < -85) latitude = -85;
  while (longitude > 180) longitude -= 360;
  while (longitude < -180) longitude += 360;
}

// ---------- tiles ----------

struct Tile {
  int z, x, y;
  std::string png;
  uint32_t lastUsed;
};

// Recently used tiles are kept so that moving back and forth, or sliding a
// short way, does not download them again.
static const size_t MAX_TILES = 20;
static std::vector<Tile> tiles;
static uint32_t useCounter = 0;

static const std::string *loadTile(int z, int x, int y, bool &downloaded) {
  downloaded = false;
  for (auto &t : tiles) {
    if (t.z == z && t.x == x && t.y == y) {
      t.lastUsed = ++useCounter;
      return &t.png;
    }
  }

  char url[96];
  snprintf(url, sizeof(url), "https://tile.openstreetmap.org/%d/%d/%d.png", z, x, y);
  std::string png;
  int code = platformDownload(url, png);
  downloaded = true;
  if (code != 200 || png.empty()) {
    printf("Maps: tile %d/%d/%d failed (%d)\n", z, x, y, code);
    problem = platformOnline() ? "Could not load the map" : "Not connected to " + platformNetworkName();
    return nullptr;
  }

  if (tiles.size() >= MAX_TILES) {  // make room by dropping the least recently used
    size_t oldest = 0;
    for (size_t i = 1; i < tiles.size(); i++) {
      if (tiles[i].lastUsed < tiles[oldest].lastUsed) oldest = i;
    }
    tiles.erase(tiles.begin() + oldest);
  }
  tiles.push_back({z, x, y, std::move(png), ++useCounter});
  return &tiles.back().png;
}

// ---------- drawing: the map ----------

static std::string fit(std::string s, int maxWidth, const lgfx::IFont *font) {
  if (lcd.textWidth(s.c_str(), font) <= maxWidth) return s;
  while (s.size() > 1 && lcd.textWidth((s + "..").c_str(), font) > maxWidth) s.pop_back();
  return s + "..";
}

static void drawMagnifier(int cx, int cy, uint32_t ink) {
  lcd.fillSmoothCircle(cx - 2, cy - 2, 6, ink);
  lcd.fillSmoothCircle(cx - 2, cy - 2, 4, colour::bar);
  lcd.drawWideLine(cx + 3, cy + 3, cx + 7, cy + 7, 1.2f, ink);
}

// The search bar above the map: tapping it opens the search.
static void drawBar() {
  lcd.fillRect(0, TITLE_H, lcd.width(), MAP_TOP - TITLE_H, colour::background);
  lcd.fillSmoothRoundRect(6, BAR_TOP, lcd.width() - 12, BAR_H - 4, 15, colour::bar);
  drawMagnifier(24, BAR_TOP + 15, colour::dim);
  lcd.setTextDatum(middle_left);
  lcd.setTextColor(pinShown ? colour::text : colour::dim);
  lcd.drawString(fit(pinShown ? pinName : "Search for a place", lcd.width() - 60, FONT_BODY).c_str(),
                 40, BAR_TOP + 15, FONT_BODY);
}

// Things drawn on top of the map: zoom buttons, credit, and any message.
static void drawOverlays() {
  for (int i = 0; i < 2; i++) {
    int y = i == 0 ? ZOOM_IN_Y : ZOOM_OUT_Y;
    bool atLimit = i == 0 ? zoom >= MAX_ZOOM : zoom <= MIN_ZOOM;
    lcd.fillSmoothRoundRect(BUTTON_X, y, BUTTON, BUTTON, 10, colour::bar);
    uint32_t ink = atLimit ? colour::rule : colour::text;
    int cx = BUTTON_X + BUTTON / 2, cy = y + BUTTON / 2;
    lcd.fillRect(cx - 9, cy - 1, 19, 3, ink);
    if (i == 0) lcd.fillRect(cx - 1, cy - 9, 3, 19, ink);
  }

  // OpenStreetMap asks for this credit wherever its map is shown.
  const char *credit = "(c) OpenStreetMap contributors";
  int w = lcd.textWidth(credit, FONT_SMALL) + 8;
  lcd.fillRect(0, lcd.height() - 13, w, 13, colour::background);
  lcd.setTextDatum(top_left);
  lcd.setTextColor(colour::dim);
  lcd.drawString(credit, 4, lcd.height() - 11, FONT_SMALL);

  if (!problem.empty()) {
    int boxW = lcd.textWidth(problem.c_str(), FONT_BODY) + 24;
    lcd.fillSmoothRoundRect((lcd.width() - boxW) / 2, MAP_TOP + 12, boxW, 28, 14, colour::bar);
    lcd.setTextDatum(middle_center);
    lcd.setTextColor(colour::red);
    lcd.drawString(problem.c_str(), lcd.width() / 2, MAP_TOP + 26, FONT_BODY);
  }
}

// Copies the in-memory view to the screen, shifted by dx, dy.
static void showView(int dx, int dy) {
  lcd.setClipRect(0, MAP_TOP, lcd.width(), MAP_H);
  // Whatever the shifted view no longer covers is filled in plain.
  if (dx > 0) lcd.fillRect(0, MAP_TOP, dx, MAP_H, COLOUR_LAND);
  if (dx < 0) lcd.fillRect(lcd.width() + dx, MAP_TOP, -dx, MAP_H, COLOUR_LAND);
  if (dy > 0) lcd.fillRect(0, MAP_TOP, lcd.width(), dy, COLOUR_LAND);
  if (dy < 0) lcd.fillRect(0, MAP_TOP + MAP_H + dy, lcd.width(), -dy, COLOUR_LAND);
  view.pushSprite(dx, MAP_TOP + dy);
  lcd.clearClipRect();
}

// Draws the map for the current centre and zoom, downloading any tiles it
// does not have. The screen does not respond while tiles download.
static void render() {
  renderWanted = false;
  problem.clear();
  tidyCentre();

  if (!viewReady) {
    view.setColorDepth(16);
    view.setPsram(true);
    viewReady = view.createSprite(SCREEN_W, MAP_H) != nullptr;
    printf("Maps: view memory %s\n", viewReady ? "ready" : "unavailable");
  }
  // Draw into memory if there is any, otherwise straight onto the screen.
  lgfx::LovyanGFX *target = viewReady ? (lgfx::LovyanGFX *)&view : (lgfx::LovyanGFX *)&lcd;
  int originY = viewReady ? 0 : MAP_TOP;
  if (!viewReady) lcd.setClipRect(0, MAP_TOP, lcd.width(), MAP_H);
  target->fillRect(0, originY, SCREEN_W, MAP_H, COLOUR_LAND);

  // Where the view's top-left corner is on the world square.
  long long left = (long long)floor(xOf(longitude, zoom)) - SCREEN_W / 2;
  long long top = (long long)floor(yOf(latitude, zoom)) - MAP_H / 2;
  long count = 1L << zoom;

  int downloads = 0;
  for (long long ty = (long long)floor((double)top / TILE); ty * TILE < top + MAP_H; ty++) {
    for (long long tx = (long long)floor((double)left / TILE); tx * TILE < left + SCREEN_W; tx++) {
      if (ty < 0 || ty >= count) continue;  // beyond the top or bottom of the world
      int wrappedX = (int)(((tx % count) + count) % count);
      bool downloaded = false;
      const std::string *png = loadTile(zoom, wrappedX, (int)ty, downloaded);
      int px = (int)(tx * TILE - left), py = (int)(ty * TILE - top) + originY;
      if (png) target->drawPng((const uint8_t *)png->data(), png->size(), px, py);
      if (downloaded) {
        downloads++;
        if (viewReady) showView(0, 0);  // show progress as each tile arrives
      }
    }
  }

  if (pinShown) {
    int px = (int)(xOf(pinLongitude, zoom) - left), py = (int)(yOf(pinLatitude, zoom) - top) + originY;
    if (px > -20 && px < SCREEN_W + 20 && py > originY - 10 && py < originY + MAP_H + 40) {
      target->fillTriangle(px - 8, py - 16, px + 8, py - 16, px, py, COLOUR_PIN);
      target->fillSmoothCircle(px, py - 22, 11, COLOUR_PIN);
      target->fillSmoothCircle(px, py - 22, 4, 0xFFFFFFu);
    }
  }

  if (viewReady) {
    showView(0, 0);
  } else {
    lcd.clearClipRect();
  }
  viewLoaded = true;
  drawOverlays();
  printf("Maps: showing %.4f, %.4f at zoom %d (%d tiles downloaded)\n", latitude, longitude, zoom,
         downloads);
}

static void saveView() {
  char buf[64];
  snprintf(buf, sizeof(buf), "%.5f|%.5f|%d", latitude, longitude, zoom);
  platformSaveSetting("map_view", buf);
}

static void loadView() {
  static bool done = false;
  if (done) return;
  done = true;
  std::string saved = platformLoadSetting("map_view");
  double lat, lon;
  int z;
  if (sscanf(saved.c_str(), "%lf|%lf|%d", &lat, &lon, &z) == 3 && z >= MIN_ZOOM && z <= MAX_ZOOM) {
    latitude = lat;
    longitude = lon;
    zoom = z;
  }
}

static void drawMapScreen() {
  lcd.startWrite();
  drawBar();
  if (viewLoaded && viewReady) {
    showView(0, 0);
  } else {
    lcd.fillRect(0, MAP_TOP, lcd.width(), MAP_H, COLOUR_LAND);
  }
  drawOverlays();
  lcd.endWrite();
}

// ---------- searching for a place ----------

static const int CHOICE_TOP = TITLE_H + 44;
static const int CHOICE_H = 36;
static const int INPUT_H = 40;
static const int INPUT_TOP = keyboard::TOP - INPUT_H;

// Makes text safe to draw: the fonts only cover ASCII.
static std::string plain(const char *s) {
  std::string out;
  for (; s && *s; s++) {
    unsigned char c = (unsigned char)*s;
    if (c >= 32 && c < 127) {
      out += (char)c;
    } else if (c >= 127 && (c & 0xC0) != 0x80) {  // one '?' per accented character
      out += '?';
    }
  }
  return out;
}

static void drawInput() {
  lcd.startWrite();
  lcd.fillRect(0, INPUT_TOP, lcd.width(), INPUT_H, colour::background);
  lcd.fillSmoothRoundRect(6, INPUT_TOP + 4, lcd.width() - 12, INPUT_H - 8, 10, colour::bar);
  int cy = INPUT_TOP + INPUT_H / 2;
  drawMagnifier(24, cy, colour::dim);
  int x = 40;
  lcd.setTextDatum(middle_left);
  if (query.empty()) {
    lcd.setTextColor(colour::dim);
    lcd.drawString("Place or address", x + 4, cy, &fonts::DejaVu18);
  } else {
    // Show the end of the text when it is too long for the box.
    std::string shown = query;
    while (lcd.textWidth(shown.c_str(), &fonts::DejaVu18) > lcd.width() - 70) shown.erase(0, 1);
    lcd.setTextColor(colour::text);
    lcd.drawString(shown.c_str(), x, cy, &fonts::DejaVu18);
    x += lcd.textWidth(shown.c_str(), &fonts::DejaVu18) + 2;
  }
  lcd.fillRect(x, cy - 10, 2, 20, colour::green);
  lcd.endWrite();
}

static void drawChoices() {
  lcd.startWrite();
  lcd.fillRect(0, TITLE_H, lcd.width(), INPUT_TOP - TITLE_H, colour::background);
  lcd.setTextDatum(middle_left);
  lcd.setTextColor(colour::dim);
  lcd.drawString(fit(searchHint, lcd.width() - 100, FONT_BODY).c_str(), MARGIN, TITLE_H + 23,
                 FONT_BODY);
  const int buttonW = 66;
  int buttonX = lcd.width() - 8 - buttonW;
  lcd.fillSmoothRoundRect(buttonX, TITLE_H + 10, buttonW, 26, 13, colour::rule);
  lcd.setTextDatum(middle_center);
  lcd.setTextColor(colour::text);
  lcd.drawString("Cancel", buttonX + buttonW / 2, TITLE_H + 23, FONT_BODY);

  for (size_t i = 0; i < choices.size(); i++) {
    int y = CHOICE_TOP + (int)i * CHOICE_H;
    lcd.fillSmoothRoundRect(6, y + 2, lcd.width() - 12, CHOICE_H - 4, 10, colour::bar);
    lcd.setTextDatum(middle_left);
    lcd.setTextColor(colour::text);
    lcd.drawString(fit(choices[i].name, lcd.width() - 36, FONT_BODY).c_str(), 18, y + CHOICE_H / 2,
                   FONT_BODY);
  }
  lcd.endWrite();
}

static void openSearch() {
  searching = true;
  query.clear();
  choices.clear();
  searchHint = "Type a place, then Search";
  drawChoices();
  drawInput();
  keyboard::open("Search", true);
}

static void closeSearch() {
  searching = false;
  drawMapScreen();
}

// Looks the typed text up and lists the places found.
static void search() {
  // Web addresses may only contain certain characters; the rest are encoded.
  std::string encoded;
  char buf[4];
  for (unsigned char c : query) {
    if (isalnum(c)) {
      encoded += (char)c;
    } else {
      snprintf(buf, sizeof(buf), "%%%02X", c);
      encoded += buf;
    }
  }

  searchHint = "Searching...";
  choices.clear();
  drawChoices();
  printf("Maps: searching for %s\n", query.c_str());

  JsonDocument filter, doc;
  JsonObject f = filter.add<JsonObject>();
  f["display_name"] = true;
  f["lat"] = true;
  f["lon"] = true;
  f["boundingbox"] = true;
  int code = platformJsonGet("https://nominatim.openstreetmap.org/search?q=" + encoded +
                                 "&format=jsonv2&limit=5&accept-language=en",
                             doc, filter);
  if (code != 200) {
    searchHint = platformOnline() ? "Could not reach the search" : "Not connected";
  } else {
    for (JsonObjectConst result : doc.as<JsonArrayConst>()) {
      std::string name = plain(result["display_name"] | "");
      if (name.empty()) continue;
      Place p;
      p.name = name;
      // The service sends its numbers as text.
      p.latitude = atof(result["lat"] | "0");
      p.longitude = atof(result["lon"] | "0");
      JsonArrayConst box = result["boundingbox"];
      p.south = atof(box[0] | "0");
      p.north = atof(box[1] | "0");
      p.west = atof(box[2] | "0");
      p.east = atof(box[3] | "0");
      choices.push_back(p);
    }
    searchHint = choices.empty() ? "Nothing found" : "Tap a place to go there";
  }
  printf("Maps: %d places found\n", (int)choices.size());
  drawChoices();
}

// Moves the map to a place, zoomed so that the whole of it is in view.
static void goTo(const Place &p) {
  latitude = p.latitude;
  longitude = p.longitude;
  pinShown = true;
  pinLatitude = p.latitude;
  pinLongitude = p.longitude;
  pinName = p.name;

  // Each zoom level doubles the scale, so the zoom that fits a span is a
  // logarithm of how many times that span goes into the whole world.
  double lonSpan = fabs(p.east - p.west), latSpan = fabs(p.north - p.south);
  double fitWidth = lonSpan > 0 ? log2(360.0 / lonSpan * SCREEN_W / TILE) : MAX_ZOOM;
  double fitHeight = latSpan > 0 ? log2(170.0 / latSpan * MAP_H / TILE) : MAX_ZOOM;
  zoom = (int)floor(fitWidth < fitHeight ? fitWidth : fitHeight);
  if (zoom > 17) zoom = 17;  // close enough for a single building
  if (zoom < 3) zoom = 3;

  printf("Maps: going to %s\n", p.name.c_str());
  saveView();
  searching = false;
  viewLoaded = false;
  renderWanted = true;
  drawMapScreen();
}

static void updateSearch(const Touch &touch) {
  if (touch.pressed && touch.y > TITLE_H && touch.y < CHOICE_TOP && touch.x > lcd.width() - 90) {
    closeSearch();
    return;
  }
  if (touch.pressed && touch.y >= CHOICE_TOP && touch.y < INPUT_TOP) {
    size_t index = (touch.y - CHOICE_TOP) / CHOICE_H;
    if (index < choices.size()) goTo(Place(choices[index]));
    return;
  }

  char typed = 0;
  switch (keyboard::update(touch, &typed)) {
    case keyboard::TYPED:
      if (query.size() < 60) query += typed;
      drawInput();
      break;
    case keyboard::BACKSPACE:
      if (!query.empty()) query.pop_back();
      drawInput();
      break;
    case keyboard::ACTION:
      if (query.find_first_not_of(' ') != std::string::npos) search();
      break;
    case keyboard::NONE:
      break;
  }
}

// ---------- app ----------

static void enter() {
  loadView();
  searching = false;
  dragging = false;
  drawMapScreen();
  renderWanted = !viewLoaded || !problem.empty();
}

static void leave() { saveView(); }

static void changeZoom(int by) {
  int z = zoom + by;
  if (z < MIN_ZOOM || z > MAX_ZOOM) return;
  zoom = z;
  render();
}

static void update(const Touch &touch) {
  if (searching) {
    updateSearch(touch);
    return;
  }
  if (renderWanted) {
    render();
    return;
  }

  if (touch.pressed) {
    if (touch.y > TITLE_H && touch.y < MAP_TOP) {
      openSearch();
      return;
    }
    if (touch.x >= BUTTON_X - 4 && touch.y >= ZOOM_IN_Y - 4) {
      changeZoom(touch.y < ZOOM_OUT_Y - 3 ? 1 : -1);
      return;
    }
    if (touch.y >= MAP_TOP) {
      dragging = true;
      dragStartX = dragX = touch.x;
      dragStartY = dragY = touch.y;
    }
    return;
  }

  if (!dragging) return;
  int dx = touch.x - dragStartX, dy = touch.y - dragStartY;
  if (touch.down) {
    // Slide the picture along with the finger.
    if (viewReady && viewLoaded && (abs(touch.x - dragX) + abs(touch.y - dragY) >= 2)) {
      dragX = touch.x;
      dragY = touch.y;
      showView(dx, dy);
    }
    return;
  }

  // The finger has lifted: move the centre by the distance dragged and
  // draw the map properly, fetching whatever has come into view.
  dragging = false;
  if (abs(dx) + abs(dy) < 6) {  // a tap, not a drag
    if (viewReady && viewLoaded) showView(0, 0);
    drawOverlays();
    return;
  }
  double x = xOf(longitude, zoom) - dx, y = yOf(latitude, zoom) - dy;
  double size = worldSize(zoom);
  if (y < 0) y = 0;
  if (y > size - 1) y = size - 1;
  longitude = longitudeOf(x, zoom);
  latitude = latitudeOf(y, zoom);
  render();
}

const App mapsApp = {"Maps", drawMapsIcon, enter, update, leave};

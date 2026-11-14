// App logos, drawn from shapes so they stay sharp and take no storage.
// Each fills an ICON_SIZE square whose top-left corner is at x, y.

#include "apps.h"

static const int CORNER = 18;

static void tile(int x, int y, uint32_t fill) {
  lcd.fillSmoothRoundRect(x, y, ICON_SIZE, ICON_SIZE, CORNER, fill);
}

// Calculator: an orange tile with a display strip above two rows of keys.
void drawCalculatorIcon(int x, int y) {
  tile(x, y, 0xFF9500u);
  lcd.fillSmoothRoundRect(x + 14, y + 13, 48, 15, 4, 0xFFFFFFu);
  for (int row = 0; row < 2; row++) {
    for (int col = 0; col < 3; col++) {
      bool equalsKey = row == 1 && col == 2;
      lcd.fillSmoothRoundRect(x + 14 + col * 18, y + 35 + row * 17, 12, 12, 3,
                              equalsKey ? 0x3A2400u : 0xFFFFFFu);
    }
  }
}

// Messages: a green tile with a white speech bubble and three dots.
void drawMessagesIcon(int x, int y) {
  const uint32_t green = 0x34C759u;
  tile(x, y, green);
  lcd.fillSmoothRoundRect(x + 13, y + 16, 50, 34, 13, 0xFFFFFFu);
  lcd.fillTriangle(x + 22, y + 46, x + 19, y + 62, x + 38, y + 49, 0xFFFFFFu);
  for (int i = 0; i < 3; i++) {
    lcd.fillSmoothCircle(x + 26 + i * 12, y + 33, 3, green);
  }
}

// Drawing: an indigo tile with a pencil that has just drawn a curved stroke.
void drawDrawingIcon(int x, int y) {
  tile(x, y, 0x5E5CE6u);
  // The stroke, drawn a few times over to thicken it.
  for (int i = 0; i < 3; i++) {
    lcd.drawBezier(x + 20, y + 57 + i, x + 34, y + 68 + i, x + 44, y + 50 + i, x + 60, y + 60 + i,
                   0xFFFFFFu);
  }
  // Pencil, pointing down and to the left: wood tip, yellow body, pink eraser.
  lcd.fillTriangle(x + 19, y + 57, x + 25, y + 43, x + 33, y + 51, 0xF2D2A9u);
  lcd.fillTriangle(x + 19, y + 57, x + 21, y + 52, x + 24, y + 55, 0x2C2C2Eu);
  lcd.drawWideLine(x + 31, y + 45, x + 50, y + 26, 5.5f, 0xFFD60Au);
  lcd.drawWideLine(x + 52, y + 24, x + 56, y + 20, 5.5f, 0xFF6482u);
}

// GitHub: a dark tile with a branch joining a main line of commits.
void drawGithubIcon(int x, int y) {
  const uint32_t fill = 0x2D333Bu;
  const uint32_t white = 0xFFFFFFu;
  const uint32_t green = 0x3FB950u;
  tile(x, y, 0x545D68u);  // lighter rim so the dark tile stands out on black
  lcd.fillSmoothRoundRect(x + 2, y + 2, ICON_SIZE - 4, ICON_SIZE - 4, CORNER - 2, fill);

  lcd.drawWideLine(x + 26, y + 20, x + 26, y + 56, 2.0f, white);    // main line
  lcd.drawWideLine(x + 50, y + 30, x + 50, y + 36, 2.0f, green);    // branch
  lcd.drawWideLine(x + 50, y + 36, x + 26, y + 48, 2.0f, green);    // joining main

  const int nodes[3][2] = {{26, 20}, {26, 56}, {50, 28}};
  for (int i = 0; i < 3; i++) {
    lcd.fillSmoothCircle(x + nodes[i][0], y + nodes[i][1], 7, i == 2 ? green : white);
    lcd.fillSmoothCircle(x + nodes[i][0], y + nodes[i][1], 3, fill);
  }
}

// Weather: a sky-blue tile with the sun coming out from behind a cloud.
void drawWeatherIcon(int x, int y) {
  const uint32_t sun = 0xFFD60Au;
  const uint32_t cloud = 0xFFFFFFu;
  tile(x, y, 0x2F9BE8u);

  int sx = x + 48, sy = y + 27;
  lcd.fillSmoothCircle(sx, sy, 11, sun);
  const int rays[8][2] = {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}};
  for (int i = 0; i < 8; i++) {
    float len = rays[i][0] && rays[i][1] ? 0.707f : 1.0f;  // same length on the diagonals
    lcd.drawWideLine(sx + (int)(rays[i][0] * 15 * len), sy + (int)(rays[i][1] * 15 * len),
                     sx + (int)(rays[i][0] * 19 * len), sy + (int)(rays[i][1] * 19 * len), 1.5f, sun);
  }

  lcd.fillSmoothCircle(x + 24, y + 46, 10, cloud);
  lcd.fillSmoothCircle(x + 37, y + 38, 13, cloud);
  lcd.fillSmoothCircle(x + 51, y + 47, 9, cloud);
  lcd.fillSmoothRoundRect(x + 14, y + 45, 47, 12, 6, cloud);
}

// Calendar: a white page with a red binding across the top, two rings, and
// rows of days with one picked out.
void drawCalendarIcon(int x, int y) {
  const uint32_t red = 0xFF453Au;
  tile(x, y, 0xFFFFFFu);
  // The red top: a rounded tile, with its lower part covered back over in white.
  lcd.fillSmoothRoundRect(x, y, ICON_SIZE, 36, CORNER, red);
  lcd.fillRect(x, y + 22, ICON_SIZE, 14, 0xFFFFFFu);
  for (int i = 0; i < 2; i++) {
    lcd.fillSmoothRoundRect(x + 20 + i * 30, y + 5, 6, 12, 3, 0xFFFFFFu);
  }
  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 4; col++) {
      bool picked = row == 1 && col == 2;
      lcd.fillSmoothRoundRect(x + 11 + col * 14, y + 29 + row * 13, 10, 9, 2,
                              picked ? red : 0xC7C7CCu);
    }
  }
}

// World Clock: a teal tile with a clock face that is also a globe, its
// hands showing ten past ten.
void drawWorldClockIcon(int x, int y) {
  const uint32_t face = 0xFFFFFFu, line = 0x9AD8D0u, hand = 0x0B3B37u;
  tile(x, y, 0x12A594u);
  int cx = x + ICON_SIZE / 2, cy = y + ICON_SIZE / 2;
  lcd.fillSmoothCircle(cx, cy, 27, face);

  // The globe: an equator, two lines of latitude and a curved meridian pair.
  lcd.drawFastHLine(cx - 26, cy, 53, line);
  lcd.drawFastHLine(cx - 22, cy - 14, 45, line);
  lcd.drawFastHLine(cx - 22, cy + 14, 45, line);
  lcd.drawEllipse(cx, cy, 12, 26, line);

  lcd.drawWideLine(cx, cy, cx - 9, cy - 9, 2.0f, hand);   // hour hand
  lcd.drawWideLine(cx, cy, cx + 8, cy - 17, 1.4f, hand);  // minute hand
  lcd.fillSmoothCircle(cx, cy, 3, 0xFF9500u);
}

// Maps: a cream map with a park, a river and a road, and a red pin.
void drawMapsIcon(int x, int y) {
  const uint32_t paper = 0xF4EBD3u, park = 0x9CD67Bu, water = 0x6CB8F0u, road = 0xFFFFFFu;
  const uint32_t roadEdge = 0xD9CDAEu, pin = 0xFF453Au;
  tile(x, y, paper);
  lcd.fillSmoothRoundRect(x + 8, y + 40, 26, 26, 8, park);
  lcd.drawWideLine(x + 10, y + 14, x + 34, y + 30, 4.0f, water);
  lcd.drawWideLine(x + 34, y + 30, x + 66, y + 26, 4.0f, water);
  lcd.drawWideLine(x + 46, y + 68, x + 22, y + 8, 4.5f, roadEdge);
  lcd.drawWideLine(x + 46, y + 68, x + 22, y + 8, 3.0f, road);

  // The pin: a disc with a point beneath it and a white centre.
  int px = x + 52, py = y + 40;
  lcd.fillTriangle(px - 9, py + 4, px + 9, py + 4, px, py + 20, pin);
  lcd.fillSmoothCircle(px, py, 11, pin);
  lcd.fillSmoothCircle(px, py, 4, 0xFFFFFFu);
}

// Doubles: a navy tile holding four game tiles, each with twice the dots of
// the one before.
void drawDoublesIcon(int x, int y) {
  const uint32_t tiles[4] = {0x1F6378u, 0x23A877u, 0xF2A53Au, 0xE8476Au};
  const int dots[4] = {1, 2, 4, 8};
  tile(x, y, 0x15203Au);
  for (int i = 0; i < 4; i++) {
    int tx = x + 10 + (i % 2) * 30, ty = y + 10 + (i / 2) * 30;
    lcd.fillSmoothRoundRect(tx, ty, 26, 26, 6, tiles[i]);
    // Dots sit in a grid: one row for 1 or 2, two rows for 4 and 8.
    int rows = dots[i] > 2 ? 2 : 1, perRow = dots[i] / rows;
    for (int r = 0; r < rows; r++) {
      for (int c = 0; c < perRow; c++) {
        int dx = tx + 13 + (int)((c - (perRow - 1) / 2.0f) * (perRow > 2 ? 5 : 8));
        int dy = ty + 13 + (int)((r - (rows - 1) / 2.0f) * 8);
        lcd.fillSmoothCircle(dx, dy, perRow > 2 ? 1 : 2, 0xFFFFFFu);
      }
    }
  }
}

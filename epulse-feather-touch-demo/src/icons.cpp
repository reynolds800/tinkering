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

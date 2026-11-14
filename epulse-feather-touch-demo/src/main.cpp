// Simple demo for the ThingPulse ePulse Feather with the 3.5" ILI9488 display
// and FT6236 capacitive touch: shows colour bars and lets you draw with a finger.

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Wire.h>

// Portrait, same orientation as the ThingPulse sample. Raw touch coordinates
// are rotated 180 degrees from this rotation, so both axes are flipped.
static const uint8_t TFT_ROTATION = 2;
static const bool TOUCH_FLIP_X = true;
static const bool TOUCH_FLIP_Y = true;

static const uint8_t FT6236_ADDR = 0x38;
static const uint8_t FT6236_REG_NUM_TOUCHES = 0x02;
static const uint8_t FT6236_REG_THRESHOLD = 0x80;
static const uint8_t FT6236_REG_CHIP_ID = 0xA3;

static const int HEADER_H = 60;
static const int BARS_H = 30;
static const int CANVAS_Y = HEADER_H + BARS_H;
static const int BTN_W = 90, BTN_H = 36, BTN_MARGIN = 12;

TFT_eSPI tft;
bool touchOk = false;

static const uint16_t PALETTE[] = {TFT_RED,  TFT_ORANGE, TFT_YELLOW, TFT_GREEN,
                                   TFT_CYAN, TFT_BLUE,   TFT_MAGENTA, TFT_WHITE};
static const int PALETTE_N = sizeof(PALETTE) / sizeof(PALETTE[0]);
uint16_t penColour = TFT_YELLOW;

bool touchBegin() {
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  Wire.beginTransmission(FT6236_ADDR);
  Wire.write(FT6236_REG_CHIP_ID);
  if (Wire.endTransmission() != 0) return false;
  Wire.requestFrom(FT6236_ADDR, (uint8_t)1);
  uint8_t chipId = Wire.read();
  Serial.printf("Touch controller chip id: 0x%02X\n", chipId);

  Wire.beginTransmission(FT6236_ADDR);
  Wire.write(FT6236_REG_THRESHOLD);
  Wire.write(40);  // sensitivity; lower = more sensitive
  Wire.endTransmission();
  return true;
}

// Returns true and fills x/y (screen coordinates) while a finger is down.
bool touchRead(int &x, int &y) {
  Wire.beginTransmission(FT6236_ADDR);
  Wire.write(FT6236_REG_NUM_TOUCHES);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(FT6236_ADDR, (uint8_t)5) != 5) return false;

  uint8_t touches = Wire.read();
  uint8_t xh = Wire.read(), xl = Wire.read(), yh = Wire.read(), yl = Wire.read();
  if (touches == 0 || touches > 2) return false;

  x = ((xh & 0x0F) << 8) | xl;
  y = ((yh & 0x0F) << 8) | yl;
  if (TOUCH_FLIP_X) x = tft.width() - 1 - x;
  if (TOUCH_FLIP_Y) y = tft.height() - 1 - y;
  return true;
}

int clearBtnX() { return tft.width() - BTN_W - BTN_MARGIN; }

void drawUi() {
  tft.fillScreen(TFT_BLACK);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("ePulse Feather", 12, 8, 4);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString(touchOk ? "Draw with your finger" : "Touch controller not found", 12, 38, 2);

  tft.fillRoundRect(clearBtnX(), BTN_MARGIN, BTN_W, BTN_H, 6, TFT_DARKGREY);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.drawString("Clear", clearBtnX() + BTN_W / 2, BTN_MARGIN + BTN_H / 2, 2);

  // Colour bars double as a pen-colour picker.
  int barW = tft.width() / PALETTE_N;
  for (int i = 0; i < PALETTE_N; i++) {
    tft.fillRect(i * barW, HEADER_H, barW, BARS_H, PALETTE[i]);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\nePulse Feather touch demo starting");

  tft.init();
  tft.setRotation(TFT_ROTATION);
  touchOk = touchBegin();
  Serial.println(touchOk ? "FT6236 touch ready" : "FT6236 not responding on I2C");
  drawUi();
}

void loop() {
  static bool wasDown = false;
  static int lastX = 0, lastY = 0;
  static uint32_t lastLog = 0;

  int x, y;
  bool down = touchOk && touchRead(x, y);

  if (down) {
    if (millis() - lastLog > 200) {
      Serial.printf("touch x=%d y=%d\n", x, y);
      lastLog = millis();
    }

    if (y < HEADER_H) {
      if (!wasDown && x >= clearBtnX()) drawUi();
    } else if (y < CANVAS_Y) {
      int i = constrain(x / (tft.width() / PALETTE_N), 0, PALETTE_N - 1);
      penColour = PALETTE[i];
    } else {
      // Join successive points so fast strokes stay continuous.
      if (wasDown && lastY >= CANVAS_Y) {
        tft.drawWideLine(lastX, lastY, x, y, 5, penColour, TFT_BLACK);
      } else {
        tft.fillCircle(x, y, 2, penColour);
      }
    }
    lastX = x;
    lastY = y;
  }

  wasDown = down;
  delay(5);
}

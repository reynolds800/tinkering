// Drawing: finger painting on a white canvas, with a choice of colours and
// pen sizes, an eraser, undo and clear. The picture is kept while the device
// stays on, so it is still there after visiting another app.

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "apps.h"

// ---------- layout ----------

static const int TOOL_ROW_H = 44;
static const int TOOLBAR_TOP = SCREEN_H - 2 * TOOL_ROW_H;
static const int CANVAS_TOP = TITLE_H;
static const int CANVAS_H = TOOLBAR_TOP - CANVAS_TOP;

static const uint32_t PAPER = 0xFFFFFFu;
static const uint32_t PALETTE[] = {0x000000u, 0xFF3B30u, 0xFF9500u, 0xFFCC00u,
                                   0x34C759u, 0x0A84FFu, 0xAF52DEu, 0x8E5B3Au};
static const int PALETTE_N = sizeof(PALETTE) / sizeof(PALETTE[0]);
static const int SWATCH_W = SCREEN_W / PALETTE_N;

static const float PEN_RADIUS[] = {1.5f, 3.5f, 7.0f};
static const int SIZE_N = 3;

// Bottom row: three pen sizes, then eraser, undo and clear.
static const int SIZE_W = 48;
static const int ERASER_X = SIZE_N * SIZE_W, ERASER_W = 56;
static const int UNDO_X = ERASER_X + ERASER_W, UNDO_W = 56;
static const int CLEAR_X = UNDO_X + UNDO_W, CLEAR_W = SCREEN_W - CLEAR_X;

// ---------- state ----------

// The picture lives in memory and is copied to the screen, so it can be
// redrawn after leaving the app and restored by undo.
static LGFX_Sprite canvas(&lcd);
static LGFX_Sprite undoCopy(&lcd);
static bool canvasReady = false;
static bool undoReady = false;
static bool canUndo = false;

static int penColour = 0;
static int penSize = 1;
static bool erasing = false;

static bool stroking = false;
static int lastX = 0, lastY = 0;

static float radius() { return erasing ? PEN_RADIUS[penSize] * 2 + 4 : PEN_RADIUS[penSize]; }
static uint32_t ink() { return erasing ? PAPER : PALETTE[penColour]; }

// ---------- canvas ----------

static void createCanvas() {
  if (canvasReady) return;
  canvas.setColorDepth(16);
  canvas.setPsram(true);
  canvasReady = canvas.createSprite(SCREEN_W, CANVAS_H) != nullptr;
  if (canvasReady) {
    canvas.fillScreen(PAPER);
    undoCopy.setColorDepth(16);
    undoCopy.setPsram(true);
    undoReady = undoCopy.createSprite(SCREEN_W, CANVAS_H) != nullptr;
  }
  printf("Drawing: picture memory %s, undo %s\n", canvasReady ? "ready" : "unavailable",
         undoReady ? "ready" : "unavailable");
}

// Copies part of the picture to the screen.
static void showCanvas(int x, int y, int w, int h) {
  lcd.setClipRect(x, CANVAS_TOP + y, w, h);
  canvas.pushSprite(0, CANVAS_TOP);
  lcd.clearClipRect();
}

static void rememberForUndo() {
  if (!undoReady) return;
  memcpy(undoCopy.getBuffer(), canvas.getBuffer(), canvas.bufferLength());
  canUndo = true;
}

// Draws a line in picture coordinates (0, 0 is the canvas's top-left corner).
static void stroke(int x0, int y0, int x1, int y1) {
  float r = radius();
  if (!canvasReady) {
    // Without picture memory, draw straight onto the screen. Nothing is kept.
    lcd.setClipRect(0, CANVAS_TOP, SCREEN_W, CANVAS_H);
    lcd.drawWideLine(x0, CANVAS_TOP + y0, x1, CANVAS_TOP + y1, r, ink());
    lcd.clearClipRect();
    return;
  }
  canvas.drawWideLine(x0, y0, x1, y1, r, ink());
  int pad = (int)r + 2;
  int left = std::max(0, std::min(x0, x1) - pad), top = std::max(0, std::min(y0, y1) - pad);
  int right = std::min(SCREEN_W, std::max(x0, x1) + pad + 1);
  int bottom = std::min(CANVAS_H, std::max(y0, y1) + pad + 1);
  showCanvas(left, top, right - left, bottom - top);
}

static void clearPicture() {
  if (canvasReady) {
    rememberForUndo();
    canvas.fillScreen(PAPER);
    showCanvas(0, 0, SCREEN_W, CANVAS_H);
  } else {
    lcd.fillRect(0, CANVAS_TOP, SCREEN_W, CANVAS_H, PAPER);
  }
}

static void undo() {
  if (!canUndo) return;
  // Swap, so that tapping undo again brings the change back.
  uint8_t *a = (uint8_t *)canvas.getBuffer(), *b = (uint8_t *)undoCopy.getBuffer();
  std::swap_ranges(a, a + canvas.bufferLength(), b);
  showCanvas(0, 0, SCREEN_W, CANVAS_H);
}

// ---------- toolbar ----------

static void drawToolbar() {
  lcd.startWrite();
  lcd.fillRect(0, TOOLBAR_TOP, SCREEN_W, 2 * TOOL_ROW_H, colour::bar);
  lcd.drawFastHLine(0, TOOLBAR_TOP, SCREEN_W, colour::rule);

  // Colours. The chosen one gets a white ring, unless the eraser is in use.
  int cy = TOOLBAR_TOP + TOOL_ROW_H / 2 + 1;
  for (int i = 0; i < PALETTE_N; i++) {
    int cx = i * SWATCH_W + SWATCH_W / 2;
    if (i == penColour && !erasing) {
      lcd.fillSmoothCircle(cx, cy, 18, colour::text);
      lcd.fillSmoothCircle(cx, cy, 15, colour::bar);
    }
    lcd.fillSmoothCircle(cx, cy, 13, 0x636366u);  // rim, so black shows on the dark bar
    lcd.fillSmoothCircle(cx, cy, 12, PALETTE[i]);
  }

  // Pen sizes, shown as dots.
  cy = TOOLBAR_TOP + TOOL_ROW_H + TOOL_ROW_H / 2;
  for (int i = 0; i < SIZE_N; i++) {
    int x = i * SIZE_W;
    if (i == penSize) lcd.fillSmoothRoundRect(x + 4, cy - 17, SIZE_W - 8, 34, 8, colour::rule);
    lcd.fillSmoothCircle(x + SIZE_W / 2, cy, (int)(PEN_RADIUS[i] + 1.5f), colour::text);
  }

  // Eraser: a pink block with a white band.
  int ex = ERASER_X + ERASER_W / 2;
  if (erasing) lcd.fillSmoothRoundRect(ERASER_X + 4, cy - 17, ERASER_W - 8, 34, 8, colour::rule);
  lcd.fillSmoothRoundRect(ex - 13, cy - 8, 26, 16, 4, 0xFF6482u);
  lcd.fillRect(ex + 2, cy - 8, 5, 16, colour::text);

  lcd.setTextDatum(middle_center);
  lcd.setTextColor(canUndo ? colour::text : colour::dim);
  lcd.drawString("Undo", UNDO_X + UNDO_W / 2, cy, &fonts::DejaVu12);
  lcd.setTextColor(colour::text);
  lcd.drawString("Clear", CLEAR_X + CLEAR_W / 2, cy, &fonts::DejaVu12);
  lcd.endWrite();
}

static void tapToolbar(const Touch &touch) {
  if (touch.y < TOOLBAR_TOP + TOOL_ROW_H) {
    penColour = std::min(touch.x / SWATCH_W, PALETTE_N - 1);
    erasing = false;
  } else if (touch.x < ERASER_X) {
    penSize = touch.x / SIZE_W;
  } else if (touch.x < UNDO_X) {
    erasing = !erasing;
  } else if (touch.x < CLEAR_X) {
    undo();
  } else {
    clearPicture();
  }
  drawToolbar();
}

// ---------- app ----------

static void enter() {
  createCanvas();
  stroking = false;
  if (canvasReady) {
    showCanvas(0, 0, SCREEN_W, CANVAS_H);
  } else {
    lcd.fillRect(0, CANVAS_TOP, SCREEN_W, CANVAS_H, PAPER);
  }
  drawToolbar();
}

static void update(const Touch &touch) {
  if (touch.pressed) {
    if (touch.y >= TOOLBAR_TOP) {
      tapToolbar(touch);
      return;
    }
    if (touch.y >= CANVAS_TOP) {
      bool couldUndo = canUndo;
      if (canvasReady) rememberForUndo();
      stroking = true;
      lastX = touch.x;
      lastY = touch.y - CANVAS_TOP;
      stroke(lastX, lastY, lastX, lastY);  // a tap leaves a dot
      if (canUndo != couldUndo) drawToolbar();  // "Undo" lights up
      return;
    }
  }

  if (stroking && touch.down) {
    // Keep the line on the canvas if the finger slides off it.
    int x = touch.x, y = std::min(std::max(touch.y - CANVAS_TOP, 0), CANVAS_H - 1);
    if (x != lastX || y != lastY) {
      stroke(lastX, lastY, x, y);
      lastX = x;
      lastY = y;
    }
  }
  if (touch.released) stroking = false;
}

const App drawingApp = {"Drawing", drawDrawingIcon, enter, update};

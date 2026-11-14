// Doubles: a sliding puzzle. Swipe to slide every tile as far as it will go;
// two equal tiles that meet join into one worth double. A new tile appears
// after each move, and the game ends when the board is full and nothing can
// join. The game in progress and the best score are saved on the device.

#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>

#include "apps.h"
#include "platform.h"

// ---------- layout ----------

static const int SIZE = 4;  // tiles along each side of the board
static const int HEADER_TOP = TITLE_H + 8;
static const int BOARD_TOP = TITLE_H + 72;
static const int GAP = 6;
static const int CELL = 70;
static const int BOARD_W = SIZE * CELL + (SIZE + 1) * GAP;
static const int BOARD_LEFT = (SCREEN_W - BOARD_W) / 2;
static const int SWIPE = 28;  // how far a finger must move to count as a swipe

static const lgfx::IFont *const FONT_BODY = &fonts::DejaVu12;
static const lgfx::IFont *const FONT_SMALL = &fonts::DejaVu9;

static const uint32_t COLOUR_BOARD = 0x141416u;

// Tile colours run from cool to warm as the numbers grow: 1, 2, 4, 8, ...
static const uint32_t TILE_COLOURS[] = {0x27445Cu, 0x1F6378u, 0x158A86u, 0x23A877u, 0x74BF44u,
                                        0xD1C23Au, 0xF2A53Au, 0xF2713Au, 0xE8476Au, 0xC43CB0u,
                                        0x8A4BE0u, 0x4B6BF0u};
static const int TILE_COLOUR_N = sizeof(TILE_COLOURS) / sizeof(TILE_COLOURS[0]);

// ---------- state ----------

static uint32_t board[SIZE][SIZE];  // 0 is an empty square
static uint32_t shown[SIZE][SIZE];  // what is on the screen, to redraw only changes
static uint32_t score = 0, best = 0;
static bool gameOver = false;
static bool loaded = false;

static bool swiping = false, swiped = false;
static int swipeX = 0, swipeY = 0;

// ---------- the game ----------

static int randomBelow(int n) {
  static std::mt19937 generator{std::random_device{}()};
  return (int)(generator() % (uint32_t)n);
}

// Puts a new tile on a random empty square: usually a 1, sometimes a 2.
static void addTile() {
  int empty = 0;
  for (int i = 0; i < SIZE * SIZE; i++) empty += board[i / SIZE][i % SIZE] == 0;
  if (empty == 0) return;
  int pick = randomBelow(empty);
  for (int i = 0; i < SIZE * SIZE; i++) {
    if (board[i / SIZE][i % SIZE] != 0) continue;
    if (pick-- == 0) {
      board[i / SIZE][i % SIZE] = randomBelow(10) == 0 ? 2 : 1;
      return;
    }
  }
}

static bool canMove() {
  for (int r = 0; r < SIZE; r++) {
    for (int c = 0; c < SIZE; c++) {
      if (board[r][c] == 0) return true;
      if (c + 1 < SIZE && board[r][c] == board[r][c + 1]) return true;
      if (r + 1 < SIZE && board[r][c] == board[r + 1][c]) return true;
    }
  }
  return false;
}

// Slides one line of four towards its start, joining equal neighbours once.
// Returns true if anything moved.
static bool slideLine(uint32_t *line[SIZE]) {
  uint32_t out[SIZE] = {0, 0, 0, 0};
  int n = 0;
  bool joined = false;  // whether the last tile placed was made by joining
  for (int i = 0; i < SIZE; i++) {
    uint32_t v = *line[i];
    if (v == 0) continue;
    if (n > 0 && out[n - 1] == v && !joined) {
      out[n - 1] = v * 2;
      score += v * 2;
      joined = true;
    } else {
      out[n++] = v;
      joined = false;
    }
  }
  bool moved = false;
  for (int i = 0; i < SIZE; i++) {
    if (*line[i] != out[i]) moved = true;
    *line[i] = out[i];
  }
  return moved;
}

// Slides the whole board. dx, dy is the direction: one of them is -1 or 1.
static bool slide(int dx, int dy) {
  bool moved = false;
  for (int i = 0; i < SIZE; i++) {
    uint32_t *line[SIZE];
    for (int j = 0; j < SIZE; j++) {
      // Lines are read starting from the edge the tiles are sliding towards.
      int along = (dx > 0 || dy > 0) ? SIZE - 1 - j : j;
      line[j] = dx != 0 ? &board[i][along] : &board[along][i];
    }
    if (slideLine(line)) moved = true;
  }
  return moved;
}

static void newGame() {
  for (auto &row : board) {
    for (auto &v : row) v = 0;
  }
  score = 0;
  gameOver = false;
  addTile();
  addTile();
}

// ---------- saving ----------

// Saved as the sixteen squares then the score, separated by commas.
static void saveGame() {
  std::string saved;
  for (int i = 0; i < SIZE * SIZE; i++) saved += std::to_string(board[i / SIZE][i % SIZE]) + ",";
  saved += std::to_string(score);
  platformSaveSetting("doubles_game", saved);
  platformSaveSetting("doubles_best", std::to_string(best));
}

static void loadGame() {
  if (loaded) return;
  loaded = true;
  best = (uint32_t)strtoul(platformLoadSetting("doubles_best").c_str(), nullptr, 10);

  std::string saved = platformLoadSetting("doubles_game");
  uint32_t values[SIZE * SIZE + 1];
  int count = 0;
  size_t at = 0;
  while (at < saved.size() && count <= SIZE * SIZE) {
    values[count++] = (uint32_t)strtoul(saved.c_str() + at, nullptr, 10);
    size_t comma = saved.find(',', at);
    if (comma == std::string::npos) break;
    at = comma + 1;
  }
  if (count != SIZE * SIZE + 1) {
    newGame();
    return;
  }
  for (int i = 0; i < SIZE * SIZE; i++) board[i / SIZE][i % SIZE] = values[i];
  score = values[SIZE * SIZE];
  gameOver = !canMove();
}

// ---------- drawing ----------

static int cellX(int col) { return BOARD_LEFT + GAP + col * (CELL + GAP); }
static int cellY(int row) { return BOARD_TOP + GAP + row * (CELL + GAP); }

static void drawTile(int row, int col) {
  uint32_t v = board[row][col];
  shown[row][col] = v;
  int x = cellX(col), y = cellY(row);
  if (v == 0) {
    lcd.fillSmoothRoundRect(x, y, CELL, CELL, 10, colour::bar);
    return;
  }

  // The colour depends on how many doublings the tile is from 1.
  int step = 0;
  for (uint32_t n = v; n > 1; n >>= 1) step++;
  bool beyond = step >= TILE_COLOUR_N;  // past the end of the colours: white
  uint32_t fill = beyond ? 0xFFFFFFu : TILE_COLOURS[step];
  // Dark numbers on the light tiles in the middle of the range, and on white.
  uint32_t ink = beyond || (step >= 4 && step <= 6) ? 0x1C1C1Eu : colour::text;

  lcd.fillSmoothRoundRect(x, y, CELL, CELL, 10, fill);
  std::string text = std::to_string(v);
  const lgfx::IFont *font = text.size() <= 3   ? &fonts::DejaVu24
                            : text.size() == 4 ? &fonts::DejaVu18
                                               : FONT_BODY;
  lcd.setTextDatum(middle_center);
  lcd.setTextColor(ink);
  lcd.drawString(text.c_str(), x + CELL / 2, y + CELL / 2, font);
}

static void drawScoreBox(int x, const char *label, uint32_t value) {
  const int w = 92, h = 48;
  lcd.fillSmoothRoundRect(x, HEADER_TOP, w, h, 10, colour::bar);
  lcd.setTextDatum(top_center);
  lcd.setTextColor(colour::dim);
  lcd.drawString(label, x + w / 2, HEADER_TOP + 6, FONT_SMALL);
  lcd.setTextColor(colour::text);
  lcd.drawString(std::to_string(value).c_str(), x + w / 2, HEADER_TOP + 21, &fonts::DejaVu18);
}

static void drawHeader() {
  drawScoreBox(8, "SCORE", score);
  drawScoreBox(106, "BEST", best);
  const int x = 212, w = SCREEN_W - 8 - x;
  lcd.fillSmoothRoundRect(x, HEADER_TOP, w, 48, 10, colour::orange);
  lcd.setTextDatum(middle_center);
  lcd.setTextColor(colour::text);
  lcd.drawString("New game", x + w / 2, HEADER_TOP + 24, FONT_BODY);
}

static void drawFooter() {
  int y = BOARD_TOP + BOARD_W + 12;
  lcd.fillRect(0, y - 4, lcd.width(), lcd.height() - y + 4, colour::background);
  lcd.setTextDatum(top_center);
  if (gameOver) {
    lcd.setTextColor(colour::red);
    lcd.drawString("No more moves", lcd.width() / 2, y, &fonts::DejaVu18);
    lcd.setTextColor(colour::dim);
    lcd.drawString("Tap New game to play again", lcd.width() / 2, y + 26, FONT_BODY);
  } else {
    lcd.setTextColor(colour::dim);
    lcd.drawString("Swipe to slide the tiles.", lcd.width() / 2, y, FONT_BODY);
    lcd.drawString("Equal tiles join and double.", lcd.width() / 2, y + 18, FONT_BODY);
  }
}

static void drawAll() {
  lcd.startWrite();
  lcd.fillRect(0, TITLE_H, lcd.width(), lcd.height() - TITLE_H, colour::background);
  drawHeader();
  lcd.fillSmoothRoundRect(BOARD_LEFT, BOARD_TOP, BOARD_W, BOARD_W, 12, COLOUR_BOARD);
  for (int r = 0; r < SIZE; r++) {
    for (int c = 0; c < SIZE; c++) drawTile(r, c);
  }
  drawFooter();
  lcd.endWrite();
}

// Redraws only the squares that have changed since they were last drawn.
static void drawChanges() {
  lcd.startWrite();
  for (int r = 0; r < SIZE; r++) {
    for (int c = 0; c < SIZE; c++) {
      if (board[r][c] != shown[r][c]) drawTile(r, c);
    }
  }
  drawHeader();
  lcd.endWrite();
}

// ---------- app ----------

static void move(int dx, int dy) {
  if (gameOver || !slide(dx, dy)) return;
  addTile();
  if (score > best) best = score;
  drawChanges();
  if (!canMove()) {
    gameOver = true;
    printf("Doubles: game over, score %u\n", (unsigned)score);
    saveGame();
    lcd.startWrite();
    drawFooter();
    lcd.endWrite();
  }
}

static void enter() {
  loadGame();
  swiping = false;
  drawAll();
}

static void leave() { saveGame(); }

static void update(const Touch &touch) {
  if (touch.pressed) {
    if (touch.y >= HEADER_TOP && touch.y < BOARD_TOP - 8 && touch.x >= 206) {
      printf("Doubles: new game\n");
      newGame();
      saveGame();
      drawAll();
      return;
    }
    if (touch.y >= BOARD_TOP - 8) {
      swiping = true;
      swiped = false;
      swipeX = touch.x;
      swipeY = touch.y;
    }
    return;
  }

  // A swipe counts as soon as the finger has gone far enough, once per touch.
  if (swiping && touch.down && !swiped) {
    int dx = touch.x - swipeX, dy = touch.y - swipeY;
    if (abs(dx) >= SWIPE || abs(dy) >= SWIPE) {
      swiped = true;
      if (abs(dx) > abs(dy)) {
        move(dx > 0 ? 1 : -1, 0);
      } else {
        move(0, dy > 0 ? 1 : -1);
      }
    }
  }
  if (touch.released) swiping = false;
}

const App doublesApp = {"Doubles", drawDoublesIcon, enter, update, leave};

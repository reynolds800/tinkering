// Simulator side: runs the app in an SDL2 window on this computer, using the
// computer's own network connection and the curl command for HTTPS.
//
//   program                       open a window; click to "tap" the screen
//   program --screenshot out.bmp  draw once, save the screen and exit

#include <SDL2/SDL.h>
#include <cstdio>
#include <string>
#include <vector>

#include "app.h"
#include "display.h"
#include "platform.h"

extern Display lcd;

static std::string screenshotPath;

std::string platformNetworkName() { return "the internet"; }

bool platformConnect() { return true; }

bool platformOnline() { return true; }

int platformGithubGet(const std::string &path, JsonDocument &doc, const JsonDocument &filter) {
  // The path is built from our own settings, but it still goes through a shell.
  if (path.find('\'') != std::string::npos) return -1;
  std::string command =
      "curl --silent --max-time 15 --user-agent simulator-github-activity-display "
      "--header 'Accept: application/vnd.github+json' --write-out '\\n%{http_code}' "
      "'https://api.github.com" + path + "'";

  FILE *pipe = popen(command.c_str(), "r");
  if (!pipe) return -1;
  std::string output;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), pipe)) > 0) output.append(buf, n);
  if (pclose(pipe) != 0) return -1;

  // curl appends the HTTP status code on a final line of its own.
  size_t split = output.rfind('\n');
  if (split == std::string::npos) return -1;
  int code = atoi(output.c_str() + split + 1);
  if (code == 200) {
    output.resize(split);
    DeserializationError err = deserializeJson(doc, output, DeserializationOption::Filter(filter));
    if (err) {
      printf("JSON error for %s: %s\n", path.c_str(), err.c_str());
      return -2;
    }
  }
  return code > 0 ? code : -1;
}

static bool saveScreenshot(const std::string &path) {
  int w = lcd.width(), h = lcd.height();
  std::vector<lgfx::rgb888_t> pixels(w * h);
  lcd.readRect(0, 0, w, h, pixels.data());
  // rgb888_t is laid out blue-first in memory.
  SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(pixels.data(), w, h, 24, w * 3,
                                                            SDL_PIXELFORMAT_BGR24);
  if (!surface) return false;
  bool ok = SDL_SaveBMP(surface, path.c_str()) == 0;
  SDL_FreeSurface(surface);
  return ok;
}

static int runApp(bool *running) {
  appSetup();
  if (!screenshotPath.empty()) {
    bool ok = saveScreenshot(screenshotPath);
    printf(ok ? "Saved %s\n" : "Could not save %s\n", screenshotPath.c_str());
    exit(ok ? 0 : 1);
  }
  while (*running) appLoop();
  return 0;
}

int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--screenshot" && i + 1 < argc) {
      screenshotPath = argv[++i];
    } else {
      fprintf(stderr, "Usage: %s [--screenshot file.bmp]\n", argv[0]);
      return 2;
    }
  }
  setvbuf(stdout, nullptr, _IOLBF, 0);
  return lgfx::Panel_sdl::main(runApp);
}

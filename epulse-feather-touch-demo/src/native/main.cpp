// Simulator side: runs the app in an SDL2 window on this computer, using the
// computer's own network connection and the curl command for HTTPS.
//
//   program                       open a window; click to "tap" the screen
//   program --name Bob            the name shown on messages sent from here
//   program --peer 192.168.4.141  also send messages directly to this address,
//                                 for a device on a different part of the network
//   program --tap 50,90 --screenshot out.bmp
//                                 tap those positions in order, save the
//                                 screen and exit (--tap may be repeated)
//   program --drag 40,100,200,300 ...
//                                 press at the first position, slide to the
//                                 second and lift; mixes with --tap in order
//   program --wait 2000 ...       keep running that many milliseconds after
//                                 the taps, before the screenshot

#include <SDL2/SDL.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "app.h"
#include "display.h"
#include "platform.h"

extern Display lcd;

static std::string screenshotPath;
// Scripted touches: a tap is a drag that starts and ends in the same place.
struct Gesture {
  int x0, y0, x1, y1;
};
static std::vector<Gesture> gestures;
static int waitMs = 0;
static std::string deviceName = "Simulator";

static const uint16_t CHAT_PORT = 47800;

std::string platformDeviceName() { return deviceName; }

// Addresses to send to directly: any given with --peer, plus any that packets
// have arrived from.
static std::vector<in_addr_t> chatPeers;

static void rememberPeer(in_addr_t address) {
  for (in_addr_t known : chatPeers) {
    if (known == address) return;
  }
  if (chatPeers.size() < 8) chatPeers.push_back(address);
}

// One UDP socket, shared for sending and receiving. Address and port reuse
// let several simulators on one computer all hear the same broadcasts.
static int chatSocket() {
  static int fd = -2;
  if (fd != -2) return fd;
  fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) return fd;
  int on = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
  setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(CHAT_PORT);
  if (bind(fd, (sockaddr *)&addr, sizeof(addr)) != 0) {
    perror("Messages: could not listen on the network");
    close(fd);
    fd = -1;
  }
  return fd;
}

bool platformChatSend(const std::string &packet) {
  int fd = chatSocket();
  if (fd < 0) return false;
  sockaddr_in to = {};
  to.sin_family = AF_INET;
  to.sin_port = htons(CHAT_PORT);
  to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
  bool sent = sendto(fd, packet.data(), packet.size(), 0, (sockaddr *)&to, sizeof(to)) ==
              (ssize_t)packet.size();
  for (in_addr_t peer : chatPeers) {
    to.sin_addr.s_addr = peer;
    sendto(fd, packet.data(), packet.size(), 0, (sockaddr *)&to, sizeof(to));
  }
  return sent;
}

bool platformChatReceive(std::string &packet) {
  int fd = chatSocket();
  if (fd < 0) return false;
  char buf[512];
  sockaddr_in from = {};
  socklen_t fromSize = sizeof(from);
  ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, (sockaddr *)&from, &fromSize);
  if (n <= 0) return false;
  rememberPeer(from.sin_addr.s_addr);
  packet.assign(buf, n);
  return true;
}

void platformBegin() {}

std::string platformNetworkName() { return "the internet"; }

bool platformOnline() { return true; }

// Settings are kept as one small file each, under ~/.config/epulse-simulator.
static std::filesystem::path settingPath(const char *key) {
  const char *config = getenv("XDG_CONFIG_HOME");
  const char *home = getenv("HOME");
  std::filesystem::path dir = config && *config ? std::filesystem::path(config)
                                                : std::filesystem::path(home ? home : ".") / ".config";
  return dir / "epulse-simulator" / key;
}

std::string platformLoadSetting(const char *key) {
  std::ifstream file(settingPath(key));
  std::string value;
  std::getline(file, value);
  return value;
}

void platformSaveSetting(const char *key, const std::string &value) {
  std::error_code ignored;
  std::filesystem::create_directories(settingPath(key).parent_path(), ignored);
  std::ofstream(settingPath(key)) << value << "\n";
}

// Runs curl and returns the HTTP status code, with the page in body.
static int curlGet(const std::string &url, const std::string &extraArgs, std::string &body) {
  // The address is built from our own settings, but it still goes through a shell.
  if (url.find('\'') != std::string::npos) return -1;
  std::string command = "curl --silent --max-time 20 --user-agent epulse-feather-apps/1.0 " +
                        extraArgs + " --write-out '\\n%{http_code}' '" + url + "'";
  FILE *pipe = popen(command.c_str(), "r");
  if (!pipe) return -1;
  body.clear();
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), pipe)) > 0) body.append(buf, n);
  if (pclose(pipe) != 0) return -1;

  // curl appends the HTTP status code on a final line of its own.
  size_t split = body.rfind('\n');
  if (split == std::string::npos) return -1;
  int code = atoi(body.c_str() + split + 1);
  body.resize(split);
  return code > 0 ? code : -1;
}

int platformHttpGet(const std::string &url,
                    const std::function<bool(const char *data, size_t size)> &onData) {
  std::string body;
  int code = curlGet(url, "", body);
  if (code == 200) {
    // Hand the page over in odd-sized pieces, as the device's network would.
    const size_t piece = 700;
    for (size_t at = 0; at < body.size(); at += piece) {
      if (!onData(body.data() + at, std::min(piece, body.size() - at))) break;
    }
  }
  return code;
}

int platformDownload(const std::string &url, std::string &body) { return curlGet(url, "", body); }

int platformJsonGet(const std::string &url, JsonDocument &doc, const JsonDocument &filter) {
  std::string body;
  int code = curlGet(url, "", body);
  if (code == 200) {
    DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
    if (err) {
      printf("JSON error for %s: %s\n", url.c_str(), err.c_str());
      return -2;
    }
  }
  return code;
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
  for (auto &g : gestures) {
    if (g.x0 == g.x1 && g.y0 == g.y1) {
      appTap(g.x0, g.y0);
    } else {
      appDrag(g.x0, g.y0, g.x1, g.y1);
    }
  }
  for (uint32_t start = SDL_GetTicks(); SDL_GetTicks() - start < (uint32_t)waitMs;) appLoop();
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
    int x, y, x1, y1;
    if (arg == "--screenshot" && i + 1 < argc) {
      screenshotPath = argv[++i];
    } else if (arg == "--tap" && i + 1 < argc && sscanf(argv[i + 1], "%d,%d", &x, &y) == 2) {
      gestures.push_back({x, y, x, y});
      i++;
    } else if (arg == "--drag" && i + 1 < argc &&
               sscanf(argv[i + 1], "%d,%d,%d,%d", &x, &y, &x1, &y1) == 4) {
      gestures.push_back({x, y, x1, y1});
      i++;
    } else if (arg == "--wait" && i + 1 < argc) {
      waitMs = atoi(argv[++i]);
    } else if (arg == "--name" && i + 1 < argc) {
      deviceName = argv[++i];
    } else if (arg == "--peer" && i + 1 < argc && inet_addr(argv[i + 1]) != INADDR_NONE) {
      rememberPeer(inet_addr(argv[++i]));
    } else {
      fprintf(stderr, "Usage: %s [--name text] [--peer address] [--tap x,y]... [--drag x0,y0,x1,y1]... [--wait ms] [--screenshot file.bmp]\n",
              argv[0]);
      return 2;
    }
  }
  setvbuf(stdout, nullptr, _IOLBF, 0);
  return lgfx::Panel_sdl::main(runApp);
}

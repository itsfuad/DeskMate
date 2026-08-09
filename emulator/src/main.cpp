#include "EmulatorPlatform.h"
#include "EmulatorDisplay.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#if defined(DESKMATE_HAVE_X11)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#endif

void setup();
void loop();

namespace {
struct Options {
  EmulatorBoard board = EmulatorBoard::Esp8266;
  EmulatorNetwork network = EmulatorNetwork::Sta;
  std::string boardName = "esp8266";
  std::string stateDirectory;
  std::string output;
  std::string responseDirectory;
  uint16_t webPort = 8080;
  uint32_t durationMs = 0;
  int scale = 2;
  int rssi = -56;
  int ldr = 640;
  bool headless = false;
};

std::atomic<bool> running{true};
void stopSignal(int) { running.store(false); }

void usage(const char* program) {
  std::cout
      << "DeskMate ESP emulator\n\n"
      << "Usage: " << program << " [options]\n\n"
      << "  --board esp8266|esp32c2|esp32\n"
      << "  --network sta|ap|offline\n"
      << "  --state-dir PATH       Persistent emulated flash\n"
      << "  --web-port PORT        Portal port (default 8080)\n"
      << "  --rssi DBM             Emulated station signal\n"
      << "  --ldr VALUE            ESP8266 ADC input\n"
      << "  --scale N              X11 scale (default 2)\n"
      << "  --headless             Run without X11\n"
      << "  --duration-ms N        Stop after N milliseconds\n"
      << "  --output FILE.bmp      Save the final framebuffer\n"
      << "  --responses DIR        Recorded raw HTTP responses for tests\n";
}

bool parse(int argc, char** argv, Options& options) {
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    auto value = [&](const char* name) -> const char* {
      if (i + 1 >= argc) {
        std::cerr << name << " requires a value\n";
        return nullptr;
      }
      return argv[++i];
    };
    if (argument == "--board") {
      const char* selected = value("--board");
      if (!selected) return false;
      options.boardName = selected;
      if (options.boardName == "esp8266") options.board = EmulatorBoard::Esp8266;
      else if (options.boardName == "esp32c2") options.board = EmulatorBoard::Esp32C2;
      else if (options.boardName == "esp32") options.board = EmulatorBoard::Esp32;
      else { std::cerr << "Unknown board: " << selected << '\n'; return false; }
    } else if (argument == "--network") {
      const char* selected = value("--network");
      if (!selected) return false;
      const std::string mode(selected);
      if (mode == "sta") options.network = EmulatorNetwork::Sta;
      else if (mode == "ap") options.network = EmulatorNetwork::Ap;
      else if (mode == "offline") options.network = EmulatorNetwork::Offline;
      else { std::cerr << "Unknown network mode: " << selected << '\n'; return false; }
    } else if (argument == "--state-dir") {
      const char* selected = value("--state-dir"); if (!selected) return false;
      options.stateDirectory = selected;
    } else if (argument == "--web-port") {
      const char* selected = value("--web-port"); if (!selected) return false;
      options.webPort = static_cast<uint16_t>(std::strtoul(selected, nullptr, 10));
    } else if (argument == "--duration-ms") {
      const char* selected = value("--duration-ms"); if (!selected) return false;
      options.durationMs = static_cast<uint32_t>(std::strtoul(selected, nullptr, 10));
    } else if (argument == "--rssi") {
      const char* selected = value("--rssi"); if (!selected) return false;
      options.rssi = std::atoi(selected);
    } else if (argument == "--ldr") {
      const char* selected = value("--ldr"); if (!selected) return false;
      options.ldr = std::atoi(selected);
    } else if (argument == "--scale") {
      const char* selected = value("--scale"); if (!selected) return false;
      options.scale = std::clamp(std::atoi(selected), 1, 8);
    } else if (argument == "--output") {
      const char* selected = value("--output"); if (!selected) return false;
      options.output = selected;
    } else if (argument == "--responses") {
      const char* selected = value("--responses"); if (!selected) return false;
      options.responseDirectory = selected;
    } else if (argument == "--headless") {
      options.headless = true;
    } else if (argument == "--help" || argument == "-h") {
      usage(argv[0]);
      std::exit(0);
    } else {
      std::cerr << "Unknown option: " << argument << '\n';
      return false;
    }
  }
  if (options.stateDirectory.empty())
    options.stateDirectory = "emulator/.state/" + options.boardName;
  if (options.headless && options.durationMs == 0) options.durationMs = 1200;
  return true;
}

#if defined(DESKMATE_HAVE_X11)
unsigned long packChannel(uint8_t channel, unsigned long mask) {
  if (!mask) return 0;
  unsigned shift = 0;
  while (((mask >> shift) & 1UL) == 0) ++shift;
  unsigned bits = 0;
  while (((mask >> shift) >> bits) & 1UL) ++bits;
  const unsigned long maximum = (1UL << bits) - 1UL;
  return ((static_cast<unsigned long>(channel) * maximum + 127UL) / 255UL) << shift;
}

void paint(XImage* image, Visual* visual) {
  const uint16_t* pixels = EmulatorDisplay::dataConst();
  for (int y = 0; y < image->height; ++y) {
    for (int x = 0; x < image->width; ++x) {
      const uint16_t color = pixels[(y * TFT_HEIGHT / image->height) * TFT_WIDTH +
                                    x * TFT_WIDTH / image->width];
      const uint8_t r5 = static_cast<uint8_t>((color >> 11) & 0x1F);
      const uint8_t g6 = static_cast<uint8_t>((color >> 5) & 0x3F);
      const uint8_t b5 = static_cast<uint8_t>(color & 0x1F);
      const uint8_t red = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
      const uint8_t green = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
      const uint8_t blue = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
      XPutPixel(image, x, y,
                packChannel(red, visual->red_mask) |
                packChannel(green, visual->green_mask) |
                packChannel(blue, visual->blue_mask));
    }
  }
}

int runWindow(const Options& options) {
  Display* display = XOpenDisplay(nullptr);
  if (!display) {
    std::cerr << "No X11 display is available; use --headless.\n";
    return 2;
  }
  const int screen = DefaultScreen(display);
  Visual* visual = DefaultVisual(display, screen);
  const int depth = DefaultDepth(display, screen);
  const int width = TFT_WIDTH * options.scale;
  const int height = TFT_HEIGHT * options.scale;
  Window window = XCreateSimpleWindow(display, RootWindow(display, screen),
      80, 80, width, height, 0, BlackPixel(display, screen), BlackPixel(display, screen));
  XSelectInput(display, window, ExposureMask | KeyPressMask | StructureNotifyMask);
  XMapWindow(display, window);
  const std::string title = "DeskMate Emulator - " + options.boardName;
  XStoreName(display, window, title.c_str());
  Atom deleteWindow = XInternAtom(display, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(display, window, &deleteWindow, 1);
  GC graphics = XCreateGC(display, window, 0, nullptr);
  XImage* image = XCreateImage(display, visual, depth, ZPixmap, 0, nullptr,
                               width, height, 32, 0);
  image->data = static_cast<char*>(std::calloc(
      static_cast<size_t>(image->bytes_per_line), height));

  const uint32_t started = millis();
  while (running.load() && !emulatorRestartRequested()) {
    while (XPending(display)) {
      XEvent event;
      XNextEvent(display, &event);
      if (event.type == ClientMessage ||
          (event.type == KeyPress &&
           (XLookupKeysym(&event.xkey, 0) == XK_Escape ||
            XLookupKeysym(&event.xkey, 0) == XK_q))) running.store(false);
    }
    loop();
    paint(image, visual);
    XPutImage(display, window, graphics, image, 0, 0, 0, 0, width, height);
    XFlush(display);
    if (options.durationMs && millis() - started >= options.durationMs) break;
  }

  XDestroyImage(image);
  XFreeGC(display, graphics);
  XDestroyWindow(display, window);
  XCloseDisplay(display);
  return 0;
}
#endif
}

int main(int argc, char** argv) {
  Options options;
  if (!parse(argc, argv, options)) { usage(argv[0]); return 1; }
  emulatorConfigure(options.board, options.network, options.rssi, options.ldr,
                    options.stateDirectory, options.webPort,
                    options.responseDirectory);
  std::signal(SIGINT, stopSignal);
  std::signal(SIGTERM, stopSignal);

  std::cout << "DeskMate emulator: " << emulatorBoardProfile().displayName
            << " | portal http://127.0.0.1:" << options.webPort << '\n';
  setup();

  int result = 0;
  if (options.headless) {
    const uint32_t started = millis();
    while (running.load() && !emulatorRestartRequested() &&
           (!options.durationMs || millis() - started < options.durationMs)) loop();
  } else {
#if defined(DESKMATE_HAVE_X11)
    result = runWindow(options);
#else
    std::cerr << "This build has no X11 support; use --headless.\n";
    result = 2;
#endif
  }
  if (!options.output.empty() &&
      !EmulatorDisplay::saveBmp(options.output, options.scale)) {
    std::cerr << "Could not save " << options.output << '\n';
    result = 3;
  }
  if (emulatorRestartRequested()) {
    std::cout << "Emulated software restart requested\n";
    return 75;
  }
  return result;
}

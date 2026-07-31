#include "PreviewFramebuffer.h"
#include "PreviewScenarios.h"
#include "Arduino.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#if defined(DESKMATE_HAVE_X11)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#endif

namespace {
struct Options {
  std::string screen = "weather-clear";
  std::string output;
  std::string allDirectory;
  int scale = 4;
  bool headless = false;
  bool list = false;
  bool pixelGrid = false;
};

void printUsage(const char* program) {
  std::cout
      << "DeskMate desktop preview\n\n"
      << "Usage:\n"
      << "  " << program << " [--screen ID] [--scale N]\n"
      << "  " << program << " --headless --screen ID --output frame.bmp\n"
      << "  " << program << " --all preview-output\n"
      << "  " << program << " --list\n\n"
      << "Interactive keys:\n"
      << "  Left/Right or J/L   Previous/next fixture\n"
      << "  W/G/N/R/O           Jump to Weather/GitHub/Network/Radar/OTA\n"
      << "  S                   Save a BMP screenshot\n"
      << "  A                   Save every fixture\n"
      << "  P                   Toggle pixel grid\n"
      << "  Q or Escape         Quit\n";
}

bool parseOptions(int argc, char** argv, Options& options) {
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    auto nextValue = [&](const char* name) -> const char* {
      if (i + 1 >= argc) {
        std::cerr << name << " requires a value\n";
        return nullptr;
      }
      return argv[++i];
    };

    if (argument == "--screen") {
      const char* value = nextValue("--screen");
      if (!value) return false;
      options.screen = value;
    } else if (argument == "--output") {
      const char* value = nextValue("--output");
      if (!value) return false;
      options.output = value;
    } else if (argument == "--all") {
      const char* value = nextValue("--all");
      if (!value) return false;
      options.allDirectory = value;
      options.headless = true;
    } else if (argument == "--scale") {
      const char* value = nextValue("--scale");
      if (!value) return false;
      options.scale = std::clamp(std::atoi(value), 1, 10);
    } else if (argument == "--headless") {
      options.headless = true;
    } else if (argument == "--list") {
      options.list = true;
    } else if (argument == "--pixel-grid") {
      options.pixelGrid = true;
    } else if (argument == "--help" || argument == "-h") {
      printUsage(argv[0]);
      std::exit(0);
    } else {
      std::cerr << "Unknown option: " << argument << "\n";
      return false;
    }
  }
  return true;
}

void renderScenario(const PreviewScenario& scenario, uint32_t nowMs) {
  PreviewFramebuffer::clear();
  previewSetMillis(nowMs);
  scenario.render(nowMs);
}

bool saveScenario(const PreviewScenario& scenario,
                  const std::filesystem::path& path, int scale,
                  uint32_t nowMs = 1250) {
  renderScenario(scenario, nowMs);
  if (!PreviewFramebuffer::saveBmp(path.string(), scale)) {
    std::cerr << "Could not write " << path << "\n";
    return false;
  }
  std::cout << "Saved " << path << "\n";
  return true;
}

bool saveAll(const std::filesystem::path& directory, int scale) {
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  bool ok = true;
  for (const PreviewScenario& scenario : previewScenarios()) {
    ok = saveScenario(scenario, directory / (scenario.id + ".bmp"), scale) && ok;
  }
  return ok;
}

#if defined(DESKMATE_HAVE_X11)
unsigned long packChannel(uint8_t channel, unsigned long mask) {
  if (!mask) return 0;
  unsigned shift = 0;
  while (((mask >> shift) & 1UL) == 0) ++shift;
  unsigned bits = 0;
  unsigned long shiftedMask = mask >> shift;
  while ((shiftedMask >> bits) & 1UL) ++bits;
  const unsigned long maximum = (1UL << bits) - 1UL;
  return ((static_cast<unsigned long>(channel) * maximum + 127UL) / 255UL)
         << shift;
}

unsigned long packRgb(Visual* visual, uint8_t red, uint8_t green,
                      uint8_t blue) {
  return packChannel(red, visual->red_mask) |
         packChannel(green, visual->green_mask) |
         packChannel(blue, visual->blue_mask);
}

void drawFramebufferToImage(XImage* image, Visual* visual, int scale,
                            bool pixelGrid) {
  const uint16_t* pixels = PreviewFramebuffer::dataConst();
  const int outputWidth = TFT_WIDTH * scale;
  const int outputHeight = TFT_HEIGHT * scale;

  for (int y = 0; y < outputHeight; ++y) {
    const int sourceY = y / scale;
    for (int x = 0; x < outputWidth; ++x) {
      const int sourceX = x / scale;
      const uint16_t color = pixels[sourceY * TFT_WIDTH + sourceX];
      const uint8_t r5 = static_cast<uint8_t>((color >> 11) & 0x1F);
      const uint8_t g6 = static_cast<uint8_t>((color >> 5) & 0x3F);
      const uint8_t b5 = static_cast<uint8_t>(color & 0x1F);
      uint8_t red = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
      uint8_t green = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
      uint8_t blue = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));

      if (pixelGrid && scale >= 4 &&
          (x % scale == scale - 1 || y % scale == scale - 1)) {
        red = static_cast<uint8_t>(red * 0.72f);
        green = static_cast<uint8_t>(green * 0.72f);
        blue = static_cast<uint8_t>(blue * 0.72f);
      }
      XPutPixel(image, x, y, packRgb(visual, red, green, blue));
    }
  }
}

void updateTitle(Display* display, Window window,
                 const PreviewScenario& scenario, size_t index) {
  std::string title = "DeskMate 240x240 Preview - " + scenario.title +
                      "  [" + std::to_string(index + 1) + "/" +
                      std::to_string(previewScenarios().size()) + "]";
  XStoreName(display, window, title.c_str());
}

int firstScenarioWithPrefix(const char* prefix) {
  const std::string wanted(prefix);
  for (size_t i = 0; i < previewScenarios().size(); ++i) {
    if (previewScenarios()[i].id.rfind(wanted, 0) == 0) {
      return static_cast<int>(i);
    }
  }
  return 0;
}

int runInteractive(const Options& options, int initialIndex) {
  Display* display = XOpenDisplay(nullptr);
  if (!display) {
    std::cerr << "No X11 display is available. Use --headless --output.\n";
    return 2;
  }

  const int screen = DefaultScreen(display);
  Visual* visual = DefaultVisual(display, screen);
  const int depth = DefaultDepth(display, screen);
  const int outputWidth = TFT_WIDTH * options.scale;
  const int outputHeight = TFT_HEIGHT * options.scale;

  Window window = XCreateSimpleWindow(
      display, RootWindow(display, screen), 80, 80, outputWidth, outputHeight,
      0, BlackPixel(display, screen), BlackPixel(display, screen));
  XSelectInput(display, window,
               ExposureMask | KeyPressMask | StructureNotifyMask);
  XMapWindow(display, window);

  Atom deleteWindow = XInternAtom(display, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(display, window, &deleteWindow, 1);

  GC graphics = XCreateGC(display, window, 0, nullptr);
  XImage* image = XCreateImage(display, visual, depth, ZPixmap, 0, nullptr,
                               outputWidth, outputHeight, 32, 0);
  if (!image) {
    XFreeGC(display, graphics);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return 3;
  }
  image->data = static_cast<char*>(std::calloc(
      static_cast<size_t>(image->bytes_per_line), outputHeight));
  if (!image->data) {
    image->data = nullptr;
    XDestroyImage(image);
    XFreeGC(display, graphics);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return 3;
  }

  int currentIndex = initialIndex;
  bool running = true;
  bool dirty = true;
  bool pixelGrid = options.pixelGrid;
  auto start = std::chrono::steady_clock::now();
  auto nextAnimation = start;

  while (running) {
    while (XPending(display)) {
      XEvent event;
      XNextEvent(display, &event);
      if (event.type == Expose) {
        dirty = true;
      } else if (event.type == ClientMessage &&
                 static_cast<Atom>(event.xclient.data.l[0]) == deleteWindow) {
        running = false;
      } else if (event.type == KeyPress) {
        const KeySym key = XLookupKeysym(&event.xkey, 0);
        const int scenarioCount = static_cast<int>(previewScenarios().size());
        if (key == XK_Escape || key == XK_q || key == XK_Q) {
          running = false;
        } else if (key == XK_Left || key == XK_j || key == XK_J) {
          currentIndex = (currentIndex + scenarioCount - 1) % scenarioCount;
          dirty = true;
        } else if (key == XK_Right || key == XK_l || key == XK_L) {
          currentIndex = (currentIndex + 1) % scenarioCount;
          dirty = true;
        } else if (key == XK_Home) {
          currentIndex = 0;
          dirty = true;
        } else if (key == XK_End) {
          currentIndex = scenarioCount - 1;
          dirty = true;
        } else if (key == XK_w || key == XK_W) {
          currentIndex = firstScenarioWithPrefix("weather-");
          dirty = true;
        } else if (key == XK_g || key == XK_G) {
          currentIndex = firstScenarioWithPrefix("github-");
          dirty = true;
        } else if (key == XK_n || key == XK_N) {
          currentIndex = firstScenarioWithPrefix("network-");
          dirty = true;
        } else if (key == XK_r || key == XK_R) {
          currentIndex = firstScenarioWithPrefix("radar-");
          dirty = true;
        } else if (key == XK_o || key == XK_O) {
          currentIndex = firstScenarioWithPrefix("ota-");
          dirty = true;
        } else if (key == XK_p || key == XK_P) {
          pixelGrid = !pixelGrid;
          dirty = true;
        } else if (key == XK_s || key == XK_S) {
          const PreviewScenario& scenario = previewScenarios()[currentIndex];
          const std::filesystem::path path =
              std::filesystem::path("preview-output") /
              (scenario.id + ".bmp");
          PreviewFramebuffer::saveBmp(path.string(), options.scale);
          std::cout << "Saved " << path << "\n";
        } else if (key == XK_a || key == XK_A) {
          saveAll("preview-output", options.scale);
          dirty = true;
        }
      }
    }

    const auto now = std::chrono::steady_clock::now();
    const uint32_t nowMs = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now - start)
            .count());
    const PreviewScenario& scenario = previewScenarios()[currentIndex];
    if (scenario.animated && now >= nextAnimation) {
      dirty = true;
      nextAnimation = now + std::chrono::milliseconds(90);
    }

    if (dirty) {
      renderScenario(scenario, nowMs);
      drawFramebufferToImage(image, visual, options.scale, pixelGrid);
      XPutImage(display, window, graphics, image, 0, 0, 0, 0, outputWidth,
                outputHeight);
      XFlush(display);
      updateTitle(display, window, scenario, currentIndex);
      dirty = false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(8));
  }

  XDestroyImage(image);
  XFreeGC(display, graphics);
  XDestroyWindow(display, window);
  XCloseDisplay(display);
  return 0;
}
#endif
}  // namespace

int main(int argc, char** argv) {
  Options options;
  if (!parseOptions(argc, argv, options)) {
    printUsage(argv[0]);
    return 1;
  }

  if (options.list) {
    for (const PreviewScenario& scenario : previewScenarios()) {
      std::cout << scenario.id << "\t" << scenario.title << "\n";
    }
    return 0;
  }

  if (!options.allDirectory.empty()) {
    return saveAll(options.allDirectory, options.scale) ? 0 : 1;
  }

  const int scenarioIndex = previewScenarioIndex(options.screen);
  if (scenarioIndex < 0) {
    std::cerr << "Unknown screen fixture: " << options.screen << "\n"
              << "Run with --list to see available fixture IDs.\n";
    return 1;
  }

  if (options.headless || !options.output.empty()) {
    const std::string output = options.output.empty()
        ? previewScenarios()[scenarioIndex].id + ".bmp"
        : options.output;
    return saveScenario(previewScenarios()[scenarioIndex], output,
                        options.scale) ? 0 : 1;
  }

#if defined(DESKMATE_HAVE_X11)
  return runInteractive(options, scenarioIndex);
#else
  std::cerr << "This build has no X11 support. Run headless or install the "
               "X11 development package and rebuild.\n";
  return 2;
#endif
}

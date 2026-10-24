// main.cpp
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "selection_monitor.hpp"

struct Options {
  int pollIntervalMs = 0; // 0 = event-driven (XInput2)
  bool showSelection = true;
  int thickness = 3;
  int fadeMs = 0;         // 0 = never hide on idle
  int fadeDurationMs = 0; // 0 = hide abruptly; needs --fade-ms > 0
};

static void printUsage(const char *argv0, std::ostream &os) {
  os << "Usage: " << argv0 << " [options]\n"
     << "Options:\n"
     << "  --poll [MS]       Poll pointer position every MS ms"
     << " (default 10) instead of using XInput2 events\n"
     << "  --no-selection    Do not display PRIMARY/CLIPBOARD text\n"
     << "  --thickness N     Crosshair line thickness in px (default 3)\n"
     << "  --fade-ms N       Hide overlay after N ms of no pointer motion"
     << " (0 = never hide, default)\n"
     << "  --fade-duration MS  Soft fade-out duration after the idle timeout"
     << " (needs --fade-ms; 0 = abrupt, default)\n"
     << "  --help            Show this help\n"
     << "Defaults reproduce the plain `x11_crosshair` behaviour.\n";
}

// Returns 0 to continue, 1 on error (usage printed), 2 if --help printed.
static int parseArgs(int argc, char **argv, Options &opts) {
  auto parseNonNegative = [](const char *name, const char *value, int &out) -> bool {
    if (!value || *value == '\0') {
      std::cerr << name << " requires a value\n";
      return false;
    }
    const std::string_view sv(value);
    int result = 0;
    const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), result);
    if (ec != std::errc() || ptr != sv.data() + sv.size()) {
      std::cerr << "Invalid " << name << " value: " << value << "\n";
      return false;
    }
    out = result;
    return true;
  };
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help") {
      printUsage(argv[0], std::cout);
      return 2;
    } else if (arg == "--poll") {
      opts.pollIntervalMs = 10;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        if (!parseNonNegative("--poll", argv[++i], opts.pollIntervalMs) ||
            opts.pollIntervalMs <= 0) {
          if (opts.pollIntervalMs == 0)
            std::cerr << "Invalid --poll interval: must be > 0\n";
          printUsage(argv[0], std::cerr);
          return 1;
        }
      }
    } else if (arg == "--no-selection") {
      opts.showSelection = false;
    } else if (arg == "--fade-ms") {
      if (!parseNonNegative("--fade-ms", (i + 1 < argc) ? argv[++i] : nullptr, opts.fadeMs)) {
        printUsage(argv[0], std::cerr);
        return 1;
      }
    } else if (arg == "--fade-duration") {
      if (!parseNonNegative("--fade-duration", (i + 1 < argc) ? argv[++i] : nullptr,
                            opts.fadeDurationMs)) {
        printUsage(argv[0], std::cerr);
        return 1;
      }
    } else if (arg == "--thickness") {
      if (!parseNonNegative("--thickness", (i + 1 < argc) ? argv[++i] : nullptr, opts.thickness) ||
          opts.thickness <= 0 || opts.thickness > 200) {
        if (opts.thickness <= 0 || opts.thickness > 200)
          std::cerr << "Invalid --thickness value: must be 1..200\n";
        printUsage(argv[0], std::cerr);
        return 1;
      }
    } else {
      std::cerr << "Unknown option: " << arg << "\n";
      printUsage(argv[0], std::cerr);
      return 1;
    }
  }
  return 0;
}
// --- Main Crosshair Class ---

// Pure idle/fade state machine; inputs in ms since epoch-like clock so it
// is testable without X.
struct IdleState {
  bool hideNow = false;
  bool fading = false;
  int alpha = 255;
};

static IdleState evaluateIdle(int fadeMs, int fadeDurationMs, long idleMsSinceMotion) {
  IdleState state;
  if (fadeMs <= 0 || idleMsSinceMotion < fadeMs) {
    return state;
  }
  const long idleMs = idleMsSinceMotion - fadeMs;
  if (fadeDurationMs > 0 && idleMs < fadeDurationMs) {
    state.fading = true;
    state.alpha = 255 - static_cast<int>(255.0 * idleMs / fadeDurationMs);
    if (state.alpha < 0) {
      state.alpha = 0;
    }
  } else {
    state.hideNow = true;
  }
  return state;
}

class CrosshairOverlay {
 public:
  explicit CrosshairOverlay(const Options &opts) : options(opts) {
    display = XOpenDisplay(nullptr);
    if (!display)
      throw std::runtime_error("Failed to open X display");

    screen = DefaultScreen(display);
    root = RootWindow(display, screen);

    // Try 32-bit visual
    XVisualInfo vinfo;
    Visual *visual = DefaultVisual(display, screen);
    int depth = DefaultDepth(display, screen);
    if (XMatchVisualInfo(display, screen, 32, TrueColor, &vinfo)) {
      visual = vinfo.visual;
      depth = vinfo.depth;
      argbVisual = true;
    }

    XSetWindowAttributes attrs;
    std::memset(&attrs, 0, sizeof(attrs));
    colormap = XCreateColormap(display, root, visual, AllocNone);
    attrs.colormap = colormap;
    attrs.border_pixel = 0;
    attrs.background_pixmap = None;
    attrs.override_redirect = True;

    unsigned long attr_mask = CWColormap | CWBorderPixel | CWBackPixmap | CWOverrideRedirect;

    window = XCreateWindow(display, root, 0, 0, DisplayWidth(display, screen),
                           DisplayHeight(display, screen), 0, depth, InputOutput, visual, attr_mask,
                           &attrs);
    if (!window)
      throw std::runtime_error("Failed to create window");

    // Click-through input region
    XserverRegion inputRegion = XFixesCreateRegion(display, nullptr, 0);
    XFixesSetWindowShapeRegion(display, window, ShapeInput, 0, 0, inputRegion);
    XFixesDestroyRegion(display, inputRegion);

    XMapRaised(display, window);
    XFlush(display);

    if (options.pollIntervalMs <= 0) {
      XSelectInput(display, root, PointerMotionMask);

      // XI2 Raw Motion (keeps tracking even when apps grab the pointer)
      int xi_opcode = 0;
      int xi_event = 0;
      int xi_error = 0;
      xi2_enabled = false;
      if (XQueryExtension(display, "XInputExtension", &xi_opcode, &xi_event, &xi_error)) {
        int major = 2;
        int minor = 0;
        if (XIQueryVersion(display, &major, &minor) == Success) {
          unsigned char mask_data[XIMaskLen(XI_RawMotion)];
          std::memset(mask_data, 0, sizeof(mask_data));
          XISetMask(mask_data, XI_RawMotion);
          XIEventMask mask{};
          mask.deviceid = XIAllMasterDevices;
          mask.mask_len = sizeof(mask_data);
          mask.mask = mask_data;
          XISelectEvents(display, root, &mask, 1);
          xi2_enabled = true;
          this->xi_opcode = xi_opcode;
        }
      }
    }

    // GCs
    gc_white = XCreateGC(display, window, 0, nullptr);
    XSetForeground(display, gc_white, fullOpacityWhite());

    gc_black = XCreateGC(display, window, 0, nullptr);
    XSetForeground(display, gc_black, fullOpacityBlack());

    // Stippled line GC
    gc_stipple = XCreateGC(display, window, 0, nullptr);
    XSetForeground(display, gc_stipple, fullOpacityBlack());
    XSetLineAttributes(display, gc_stipple, 1, LineOnOffDash, CapButt, JoinMiter);
    const char dashes[] = {1, 1};
    XSetDashes(display, gc_stipple, 0, dashes, sizeof(dashes));

    // Font
    font_struct = XLoadQueryFont(display, "fixed"); // Keep it simple/standard
    if (!font_struct)
      font_struct = XLoadQueryFont(display, "9x15");
    // If fixed/9x15 fails, XLoadQueryFont returns NULL, XSetFont handles None gracefully usually
    // but better to check. We will rely on default GC font if this fails.
    if (font_struct) {
      XSetFont(display, gc_white, font_struct->fid);
      XSetFont(display, gc_black, font_struct->fid);
    }
  }

  ~CrosshairOverlay() {
    if (font_struct)
      XFreeFont(display, font_struct);
    if (gc_white)
      XFreeGC(display, gc_white);
    if (gc_black)
      XFreeGC(display, gc_black);
    if (gc_stipple)
      XFreeGC(display, gc_stipple);
    if (window)
      XDestroyWindow(display, window);
    if (colormap)
      XFreeColormap(display, colormap);
    if (display)
      XCloseDisplay(display);
  }

  void run(SelectionMonitor *selectionMonitor) {
    const int thickness = options.thickness;
    const auto min_interval = std::chrono::milliseconds(16);
    auto last_render = std::chrono::steady_clock::now() - min_interval;
    int last_x = -1;
    int last_y = -1;
    std::vector<std::string> last_lines;
    auto last_motion = std::chrono::steady_clock::now();
    bool hidden = false;
    bool fading = false;

    // Text box settings
    const int text_margin_x = 15;
    const int text_margin_y = 15;
    const int line_height = (font_struct) ? (font_struct->ascent + font_struct->descent + 2) : 15;
    const int padding = 4;

    while (true) {
      int root_x = last_x, root_y = last_y, win_x = 0, win_y = 0;
      unsigned int mask = 0;
      Window child_return, root_return;

      if (!waitForWakeup(last_motion)) {
        continue;
      }
      if (!XQueryPointer(display, root, &root_return, &child_return, &root_x, &root_y, &win_x,
                         &win_y, &mask)) {
        continue;
      }

      // 1. Get current text strings
      std::vector<std::string> lines;
      if (selectionMonitor) {
        std::string prim, clip;
        selectionMonitor->getSelections(prim, clip);
        if (!prim.empty())
          lines.push_back("P: " + prim);
        if (!clip.empty())
          lines.push_back("C: " + clip);
      }

      const bool position_changed = (root_x != last_x || root_y != last_y);
      const bool text_changed = (lines != last_lines);
      const auto now = std::chrono::steady_clock::now();

      if (position_changed) {
        last_motion = now;
        hidden = false;
      }

      const auto idleMsSinceMotion =
          std::chrono::duration_cast<std::chrono::milliseconds>(now - last_motion).count();
      const IdleState idle =
          evaluateIdle(options.fadeMs, options.fadeDurationMs, idleMsSinceMotion);
      if (idle.hideNow) {
        if (!hidden) {
          // Idle: hide by clearing the bounding region; keep the window
          // mapped and the click-through input region untouched. Any pointer
          // motion wakes the loop and re-shows via the render path.
          applyEmptyBoundingShape();
          hidden = true;
        }
        fading = false;
        continue;
      }
      hidden = false;
      fading = idle.fading;
      if (idle.fading) {
        XSetForeground(display, gc_white, argbPixel(0xFFFFFF, idle.alpha));
        XSetForeground(display, gc_black, argbPixel(0x000000, idle.alpha));
        XSetForeground(display, gc_stipple, argbPixel(0x000000, idle.alpha));
      }

      if (!position_changed && !text_changed && !fading && (now - last_render) < min_interval) {
        continue;
      }

      last_x = root_x;
      last_y = root_y;
      last_lines = lines;
      last_render = now;

      // 2. Calculate text box geometry
      const TextBox box =
          computeTextBox(lines, root_x, root_y, text_margin_x, text_margin_y, line_height, padding);

      // 3. Define Shape Regions
      // We need max 3 rectangles: Horizontal line, Vertical line, Text Box
      int scr_w = DisplayWidth(display, screen);
      int scr_h = DisplayHeight(display, screen);
      XRectangle rects[3];
      int rect_count = 2;

      // Crosshair Horizontal
      rects[0].x = 0;
      rects[0].y = std::max(0, root_y - thickness / 2);
      rects[0].width = static_cast<unsigned short>(scr_w);
      rects[0].height = static_cast<unsigned short>(thickness);

      // Crosshair Vertical
      rects[1].x = std::max(0, root_x - thickness / 2);
      rects[1].y = 0;
      rects[1].width = static_cast<unsigned short>(thickness);
      rects[1].height = static_cast<unsigned short>(scr_h);

      // Text Box (if needed)
      if (!lines.empty()) {
        rects[2].x = box.x;
        rects[2].y = box.y;
        rects[2].width = box.w;
        rects[2].height = box.h;
        rect_count = 3;
      }

      applyBoundingShape(rects, rect_count);
      drawFrame(rects, lines, box, root_x, root_y, scr_w, scr_h, line_height, padding, fading);
      XFlush(display);
      XRaiseWindow(display, window);
      XFlush(display);
    }
  }

  bool waitForWakeup(std::chrono::steady_clock::time_point last_motion) {
    pollfd pfd{ConnectionNumber(display), POLLIN, 0};
    int timeoutMs;
    if (options.pollIntervalMs > 0) {
      // Polling mode: sleep the requested interval, then read the pointer.
      timeoutMs = options.pollIntervalMs;
    } else if (options.fadeMs > 0) {
      // Event mode with idle hiding: wake at the next idle deadline so the
      // overlay hides (or finishes fading) even without motion events.
      const auto now = std::chrono::steady_clock::now();
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(now - last_motion).count();
      const long remaining = options.fadeMs - elapsed;
      timeoutMs = static_cast<int>(remaining > 0 ? remaining : 0);
    } else {
      // Event mode: XI2 raw motion events wake us; the poll only guards
      // against a missed event so the selection box still refreshes.
      timeoutMs = 250;
    }
    poll(&pfd, 1, timeoutMs);
    XEvent ev;
    while (XPending(display)) {
      XNextEvent(display, &ev);
      if (xi2_enabled && ev.type == GenericEvent && ev.xcookie.extension == xi_opcode &&
          XGetEventData(display, &ev.xcookie)) {
        if (ev.xcookie.evtype == XI_RawMotion) {
          // We'll query absolute position in the caller.
        }
        XFreeEventData(display, &ev.xcookie);
      }
    }
    return true;
  }

  struct TextBox {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
  };

  TextBox computeTextBox(const std::vector<std::string> &lines, int root_x, int root_y,
                         int margin_x, int margin_y, int line_height, int padding) const {
    TextBox box;
    if (lines.empty()) {
      return box;
    }
    box.h = static_cast<int>(lines.size()) * line_height + (2 * padding);
    for (const auto &line : lines) {
      int w = 0;
      if (font_struct)
        w = XTextWidth(font_struct, line.c_str(), line.length());
      else
        w = static_cast<int>(line.length()) * 6;
      if (w > box.w)
        box.w = w;
    }
    box.w += (2 * padding);
    box.x = root_x + margin_x;
    box.y = root_y + margin_y;
    const int scr_w = DisplayWidth(display, screen);
    const int scr_h = DisplayHeight(display, screen);
    if (box.x + box.w > scr_w)
      box.x = root_x - box.w - margin_x;
    if (box.y + box.h > scr_h)
      box.y = root_y - box.h - margin_y;
    return box;
  }

  void applyEmptyBoundingShape() {
    XserverRegion emptyRegion = XFixesCreateRegion(display, nullptr, 0);
    XFixesSetWindowShapeRegion(display, window, ShapeBounding, 0, 0, emptyRegion);
    XFixesDestroyRegion(display, emptyRegion);
    XFlush(display);
  }

  void applyBoundingShape(XRectangle *rects, int rect_count) {
    XserverRegion bounding = XFixesCreateRegion(display, rects, rect_count);
    XFixesSetWindowShapeRegion(display, window, ShapeBounding, 0, 0, bounding);
    XFixesDestroyRegion(display, bounding);
  }

  void drawFrame(XRectangle *rects, const std::vector<std::string> &lines, const TextBox &box,
                 int root_x, int root_y, int scr_w, int scr_h, int line_height, int padding,
                 bool fading) {
    XClearWindow(display, window);
    if (!fading) {
      // Full-opacity colors; a previous fade may have left dimmed GCs.
      XSetForeground(display, gc_white, fullOpacityWhite());
      XSetForeground(display, gc_black, fullOpacityBlack());
      XSetForeground(display, gc_stipple, fullOpacityBlack());
    }

    // Draw white thick crosshair
    XFillRectangles(display, window, gc_white, rects, 2);

    // Draw stippled black lines
    XDrawLine(display, window, gc_stipple, 0, root_y, scr_w - 1, root_y);
    XDrawLine(display, window, gc_stipple, root_x, 0, root_x, scr_h - 1);

    // Draw Text Box
    if (!lines.empty()) {
      // Black background for text
      XFillRectangle(display, window, gc_black, box.x, box.y, box.w, box.h);

      // Draw text strings
      int txt_y = box.y + padding + (font_struct ? font_struct->ascent : 10);
      for (const auto &line : lines) {
        XDrawString(display, window, gc_white, box.x + padding, txt_y, line.c_str(), line.length());
        txt_y += line_height;
      }
    }
  }

  Options options;
  unsigned long argbPixel(int rgb, int alpha) const {
    return (static_cast<unsigned long>(alpha & 0xFF) << 24) | static_cast<unsigned long>(rgb);
  }
  // Full-opacity colors; on the ARGB visual use explicit pixels so the
  // fade path and the steady-state path agree.
  unsigned long fullOpacityWhite() const {
    return argbVisual ? argbPixel(0xFFFFFF, 255) : WhitePixel(display, screen);
  }
  unsigned long fullOpacityBlack() const {
    return argbVisual ? argbPixel(0x000000, 255) : BlackPixel(display, screen);
  }

 private:
  Display *display = nullptr;
  int screen = 0;
  Window root = 0;
  Window window = 0;
  Colormap colormap = 0;
  bool argbVisual = false;
  GC gc_white = nullptr;
  GC gc_black = nullptr;
  GC gc_stipple = nullptr;
  XFontStruct *font_struct = nullptr;
  int xi_opcode = 0;
  bool xi2_enabled = false;
};

int main(int argc, char **argv) {
  Options opts;
  const int rc = parseArgs(argc, argv, opts);
  if (rc == 1)
    return 1;
  if (rc == 2)
    return 0;
  try {
    CrosshairOverlay overlay(opts);
    std::unique_ptr<SelectionMonitor> selectionMonitor;
    if (opts.showSelection)
      selectionMonitor = std::make_unique<SelectionMonitor>();
    overlay.run(selectionMonitor.get());
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}

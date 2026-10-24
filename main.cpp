// main.cpp
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/XInput2.h>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstring>
#include <iostream>
#include <poll.h>

struct Options {
  int pollIntervalMs = 0; // 0 = event-driven (XInput2)
  bool showSelection = true;
  int thickness = 3;
  int fadeMs = 0; // 0 = never hide on idle
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
  auto parseNonNegative = [](const char *name, const char *value,
                             int &out) -> bool {
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
      if (!parseNonNegative("--fade-ms", (i + 1 < argc) ? argv[++i] : nullptr,
                            opts.fadeMs)) {
        printUsage(argv[0], std::cerr);
        return 1;
      }
    } else if (arg == "--fade-duration") {
      if (!parseNonNegative("--fade-duration",
                            (i + 1 < argc) ? argv[++i] : nullptr,
                            opts.fadeDurationMs)) {
        printUsage(argv[0], std::cerr);
        return 1;
      }
    } else if (arg == "--thickness") {
      if (!parseNonNegative("--thickness", (i + 1 < argc) ? argv[++i] : nullptr,
                            opts.thickness) ||
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
#include <stdexcept>
#include <thread>
#include <mutex>
#include <memory>
#include <string>
#include <vector>

// --- Helper class to fetch selections in a background thread ---
// Fetching X11 selections can block or time out. We do it in a separate thread
// so the crosshair animation remains perfectly smooth (60fps).
class SelectionMonitor {
public:
    SelectionMonitor() {
        running = true;
        monitorThread = std::thread(&SelectionMonitor::worker, this);
    }

    ~SelectionMonitor() {
        running = false;
        if (monitorThread.joinable()) {
            monitorThread.join();
        }
    }

    // Retrieve current values safely
    void getSelections(std::string &primary, std::string &clipboard) {
        std::lock_guard<std::mutex> lock(dataMutex);
        primary = primaryText;
        clipboard = clipboardText;
    }

private:
    std::thread monitorThread;
    std::mutex dataMutex;
    std::string primaryText;
    std::string clipboardText;
    std::atomic<bool> running;

    // Sanitize text: remove newlines, truncate length to keep UI clean
    std::string sanitize(const std::string &in) {
        std::string out = in;
        // Replace newlines with spaces
        std::replace(out.begin(), out.end(), '\n', ' ');
        std::replace(out.begin(), out.end(), '\r', ' ');
        // Truncate
        if (out.length() > 50) {
            out = out.substr(0, 47) + "...";
        }
        return out;
    }

    // Helper to request and read a specific selection atom
    std::string fetchOne(Display* disp, Window win, Atom selection, Atom target, Atom prop) {
        // Request conversion
        XConvertSelection(disp, selection, target, prop, win, CurrentTime);
        XFlush(disp);

        // Wait for SelectionNotify with poll-based timeout
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
        XEvent ev;
        while (std::chrono::steady_clock::now() < deadline) {
            while (XCheckTypedEvent(disp, SelectionNotify, &ev)) {
                if (ev.xselection.requestor == win && ev.xselection.selection == selection) {
                    if (ev.xselection.property == None) {
                        return "";
                    }
                    goto have_event;
                }
            }
            const auto now = std::chrono::steady_clock::now();
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
            if (remaining <= 0) {
                return "";
            }
            pollfd pfd{ConnectionNumber(disp), POLLIN, 0};
            if (poll(&pfd, 1, static_cast<int>(remaining)) <= 0) {
                return "";
            }
        }
        return "";

    have_event:
        // Read the property, looping while the server reports more data
        Atom actualType;
        int actualFormat;
        unsigned long nitems, bytesAfter;
        unsigned char *propData = nullptr;
        std::string result;
        unsigned long offset = 0;

        while (true) {
            if (XGetWindowProperty(disp, win, prop, offset, 1024, False,
                                   AnyPropertyType, &actualType, &actualFormat,
                                   &nitems, &bytesAfter, &propData) != Success) {
                break;
            }
            if (propData) {
                result.append(reinterpret_cast<char *>(propData), nitems);
                XFree(propData);
            }
            offset += nitems;
            if (bytesAfter == 0 || nitems == 0) {
                break;
            }
        }
        if (!result.empty()) {
            // Cleanup property
            XDeleteProperty(disp, win, prop);
            return result;
        }
        XDeleteProperty(disp, win, prop);
        return "";
    }

  void worker() {
        // Open a separate display connection for this thread
        Display* tDisp = XOpenDisplay(nullptr);
        if (!tDisp) return;

        // Create a hidden window to receive selection events
        Window tWin = XCreateSimpleWindow(tDisp, DefaultRootWindow(tDisp), 
                                          0, 0, 10, 10, 0, 0, 0);
        
        // Define Atoms
        // Note: XA_STRING is a macro in Xatom.h, so we don't redefine it.
        // We just use the macro directly later.
        Atom PRIMARY = XInternAtom(tDisp, "PRIMARY", False);
        Atom CLIPBOARD = XInternAtom(tDisp, "CLIPBOARD", False);
        Atom UTF8_STRING = XInternAtom(tDisp, "UTF8_STRING", False);
        Atom SEL_DATA = XInternAtom(tDisp, "XSEL_DATA", False);

        while (running.load()) {
            std::string p, c;

            // 1. Try to fetch PRIMARY (Middle click)
            // First try UTF8, fallback to STRING (using the XA_STRING macro)
            p = fetchOne(tDisp, tWin, PRIMARY, UTF8_STRING, SEL_DATA);
            if (p.empty()) p = fetchOne(tDisp, tWin, PRIMARY, XA_STRING, SEL_DATA);

            // 2. Try to fetch CLIPBOARD (Ctrl+C)
            c = fetchOne(tDisp, tWin, CLIPBOARD, UTF8_STRING, SEL_DATA);
            if (c.empty()) c = fetchOne(tDisp, tWin, CLIPBOARD, XA_STRING, SEL_DATA);

            {
                std::lock_guard<std::mutex> lock(dataMutex);
                primaryText = sanitize(p);
                clipboardText = sanitize(c);
            }

            // Poll with a timeout instead of sleeping
            pollfd pfd{ConnectionNumber(tDisp), POLLIN, 0};
            poll(&pfd, 1, 200);
        }

        XDestroyWindow(tDisp, tWin);
        XCloseDisplay(tDisp);
    }
};

// --- Main Crosshair Class ---

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
                           DisplayHeight(display, screen), 0, depth,
                           InputOutput, visual, attr_mask, &attrs);
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
    XSetForeground(display, gc_white, WhitePixel(display, screen));

    gc_black = XCreateGC(display, window, 0, nullptr);
    XSetForeground(display, gc_black, BlackPixel(display, screen));
    
    // Stippled line GC
    gc_stipple = XCreateGC(display, window, 0, nullptr);
    XSetForeground(display, gc_stipple, BlackPixel(display, screen));
    XSetLineAttributes(display, gc_stipple, 1, LineOnOffDash, CapButt, JoinMiter);
    const char dashes[] = {1, 1}; 
    XSetDashes(display, gc_stipple, 0, dashes, sizeof(dashes));

    // Font
    font_struct = XLoadQueryFont(display, "fixed"); // Keep it simple/standard
    if (!font_struct) font_struct = XLoadQueryFont(display, "9x15");
    // If fixed/9x15 fails, XLoadQueryFont returns NULL, XSetFont handles None gracefully usually 
    // but better to check. We will rely on default GC font if this fails.
    if (font_struct) {
        XSetFont(display, gc_white, font_struct->fid);
        XSetFont(display, gc_black, font_struct->fid);
    }
  }

  ~CrosshairOverlay() {
    if (font_struct) XFreeFont(display, font_struct);
    if (gc_white) XFreeGC(display, gc_white);
    if (gc_black) XFreeGC(display, gc_black);
    if (gc_stipple) XFreeGC(display, gc_stipple);
    if (window) XDestroyWindow(display, window);
    if (colormap) XFreeColormap(display, colormap);
    if (display) XCloseDisplay(display);
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

      pollfd pfd{ConnectionNumber(display), POLLIN, 0};
      if (options.pollIntervalMs > 0) {
        // Polling mode: sleep the requested interval, then read the pointer.
        poll(&pfd, 1, options.pollIntervalMs);
      } else {
        // Event mode: wake up on XI2 raw motion (bounded by min_interval).
        poll(&pfd, 1, static_cast<int>(min_interval.count()));
      }

      XEvent ev;
      while (XPending(display)) {
        XNextEvent(display, &ev);
        if (xi2_enabled && ev.type == GenericEvent &&
            ev.xcookie.extension == xi_opcode && XGetEventData(display, &ev.xcookie)) {
          if (ev.xcookie.evtype == XI_RawMotion) {
            // We'll query absolute position below.
          }
          XFreeEventData(display, &ev.xcookie);
        }
      }

      if (!XQueryPointer(display, root, &root_return, &child_return, &root_x,
                         &root_y, &win_x, &win_y, &mask)) {
        continue;
      }

      // 1. Get current text strings
      std::vector<std::string> lines;
      if (selectionMonitor) {
        std::string prim, clip;
        selectionMonitor->getSelections(prim, clip);
        if (!prim.empty()) lines.push_back("P: " + prim);
        if (!clip.empty()) lines.push_back("C: " + clip);
      }

      const bool position_changed = (root_x != last_x || root_y != last_y);
      const bool text_changed = (lines != last_lines);
      const auto now = std::chrono::steady_clock::now();

      if (position_changed) {
        last_motion = now;
        hidden = false;
      }

      if (options.fadeMs > 0 && (now - last_motion) >=
                                    std::chrono::milliseconds(options.fadeMs)) {
        const auto idleMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                now - last_motion)
                                .count() -
                            options.fadeMs;
        if (options.fadeDurationMs > 0 &&
            idleMs < options.fadeDurationMs) {
          // Soft fade: keep rendering with the crosshair alpha ramping down.
          const int alpha = 255 - static_cast<int>(
              255.0 * idleMs / options.fadeDurationMs);
          const unsigned long white = (static_cast<unsigned long>(alpha) << 24) | 0xFFFFFF;
          const unsigned long black = static_cast<unsigned long>(alpha) << 24;
          XSetForeground(display, gc_white, white);
          XSetForeground(display, gc_black, black);
          XSetForeground(display, gc_stipple, black);
          fading = true;
        } else {
          if (!hidden) {
            // Idle: hide by clearing the bounding region; keep the window
            // mapped and the click-through input region untouched.
            XserverRegion emptyRegion = XFixesCreateRegion(display, nullptr, 0);
            XFixesSetWindowShapeRegion(display, window, ShapeBounding, 0, 0,
                                       emptyRegion);
            XFixesDestroyRegion(display, emptyRegion);
            XFlush(display);
            hidden = true;
          }
          // Skip all drawing work while idle; any pointer motion (XI2 event or
          // polled movement) wakes the loop and re-shows via the render path.
          continue;
        }
      } else if (hidden || fading) {
        hidden = false;
        fading = false;
      }

      if (!position_changed && !text_changed && !fading &&
          (now - last_render) < min_interval) {
        continue;
      }

      last_x = root_x;
      last_y = root_y;
      last_lines = lines;
      last_render = now;

      // 2. Calculate text box geometry
      int box_w = 0;
      int box_h = 0;
      
      if (!lines.empty()) {
          box_h = lines.size() * line_height + (2 * padding);
          for (const auto& line : lines) {
              int w = 0;
              if (font_struct) w = XTextWidth(font_struct, line.c_str(), line.length());
              else w = line.length() * 6; // fallback estimate
              if (w > box_w) box_w = w;
          }
          box_w += (2 * padding);
      }

      int box_x = root_x + text_margin_x;
      int box_y = root_y + text_margin_y;

      // Ensure text box stays on screen
      int scr_w = DisplayWidth(display, screen);
      int scr_h = DisplayHeight(display, screen);
      if (box_x + box_w > scr_w) box_x = root_x - box_w - text_margin_x;
      if (box_y + box_h > scr_h) box_y = root_y - box_h - text_margin_y;


      // 3. Define Shape Regions
      // We need max 3 rectangles: Horizontal line, Vertical line, Text Box
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
          rects[2].x = box_x;
          rects[2].y = box_y;
          rects[2].width = box_w;
          rects[2].height = box_h;
          rect_count = 3;
      }

      // Apply Shape
      XserverRegion bounding = XFixesCreateRegion(display, rects, rect_count);
      XFixesSetWindowShapeRegion(display, window, ShapeBounding, 0, 0, bounding);
      XFixesDestroyRegion(display, bounding);

      // 4. Drawing
      XClearWindow(display, window);

      if (!fading) {
        // Full-opacity colors; a previous fade may have left dimmed GCs.
        XSetForeground(display, gc_white, WhitePixel(display, screen));
        XSetForeground(display, gc_black, BlackPixel(display, screen));
        XSetForeground(display, gc_stipple, BlackPixel(display, screen));
      }

      // Draw white thick crosshair
      XFillRectangles(display, window, gc_white, rects, 2); 
      
      // Draw stippled black lines
      XDrawLine(display, window, gc_stipple, 0, root_y, scr_w - 1, root_y);
      XDrawLine(display, window, gc_stipple, root_x, 0, root_x, scr_h - 1);

      // Draw Text Box
      if (!lines.empty()) {
          // Black background for text
          XFillRectangle(display, window, gc_black, box_x, box_y, box_w, box_h);
          
          // Draw text strings
          int txt_y = box_y + padding + (font_struct ? font_struct->ascent : 10);
          for (const auto& line : lines) {
              XDrawString(display, window, gc_white, box_x + padding, txt_y, line.c_str(), line.length());
              txt_y += line_height;
          }
      }

      XFlush(display);
      XRaiseWindow(display, window);
      XFlush(display);
    }
  }

  Options options;
private:
  Display *display = nullptr;
  int screen = 0;
  Window root = 0;
  Window window = 0;
  Colormap colormap = 0;
  GC gc_white = nullptr;
  GC gc_black = nullptr;
  GC gc_stipple = nullptr;
  XFontStruct* font_struct = nullptr;
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


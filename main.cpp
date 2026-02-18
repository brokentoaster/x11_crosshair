// main.cpp
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

class CrosshairOverlay {
public:
  CrosshairOverlay() {
    display = XOpenDisplay(nullptr);
    if (!display)
      throw std::runtime_error("Failed to open X display");

    screen = DefaultScreen(display);
    root = RootWindow(display, screen);

    // Try to use a 32-bit TrueColor visual when available.
    XVisualInfo vinfo;
    Visual *visual = DefaultVisual(display, screen);
    int depth = DefaultDepth(display, screen);
    if (XMatchVisualInfo(display, screen, 32, TrueColor, &vinfo)) {
      visual = vinfo.visual;
      depth = vinfo.depth;
    }

    // Prepare attributes: no background pixmap so the window doesn't draw a
    // filled rectangle.
    XSetWindowAttributes attrs;
    std::memset(&attrs, 0, sizeof(attrs));
    attrs.colormap = XCreateColormap(display, root, visual, AllocNone);
    attrs.border_pixel = 0;
    attrs.background_pixmap = None; // important: don't draw a filled background
    attrs.override_redirect = True; // avoid window manager decorations

    unsigned long attr_mask =
        CWColormap | CWBorderPixel | CWBackPixmap | CWOverrideRedirect;

    // Create a fullscreen, override-redirect window
    window = XCreateWindow(display, root, 0, 0, DisplayWidth(display, screen),
                           DisplayHeight(display, screen), 0, depth,
                           InputOutput, visual, attr_mask, &attrs);
    if (!window)
      throw std::runtime_error("Failed to create window");

    // Make the window click-through (no input region)
    XserverRegion inputRegion = XFixesCreateRegion(display, nullptr, 0);
    XFixesSetWindowShapeRegion(display, window, ShapeInput, 0, 0, inputRegion);
    XFixesDestroyRegion(display, inputRegion);

    // Map and raise the window so it's visible and on top
    XMapRaised(display, window);
    XFlush(display);

    // Create a GC for drawing the thick (white) crosshair
    gc = XCreateGC(display, window, 0, nullptr);
    if (!gc)
      throw std::runtime_error("Failed to create GC");
    XSetForeground(display, gc, WhitePixel(display, screen));

    // Create a second GC for the 1px stippled/dashed black line
    black_gc = XCreateGC(display, window, 0, nullptr);
    if (!black_gc)
      throw std::runtime_error("Failed to create black GC");
    XSetForeground(display, black_gc, BlackPixel(display, screen));
    // Configure the black GC to draw 1px on-off dashes (stipple/dotted effect)
    XSetLineAttributes(display, black_gc, 1, LineOnOffDash, CapButt, JoinMiter);
    const char dashes[] = {1, 1}; // 1 on, 1 off -> stippled look
    XSetDashes(display, black_gc, 0, dashes, sizeof(dashes));
  }

  ~CrosshairOverlay() {
    if (!display)
      return;
    if (gc)
      XFreeGC(display, gc);
    if (black_gc)
      XFreeGC(display, black_gc);
    if (window)
      XDestroyWindow(display, window);
    XCloseDisplay(display);
  }

  void run() {
    const int thickness = 3; // crosshair thickness in pixels
    while (true) {
      int root_x = 0, root_y = 0, win_x = 0, win_y = 0;
      unsigned int mask = 0;
      Window child_return, root_return;

      if (!XQueryPointer(display, root, &root_return, &child_return, &root_x,
                         &root_y, &win_x, &win_y, &mask)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        continue;
      }

      // Build two thin rectangles for the bounding region (horizontal +
      // vertical)
      XRectangle rects[2];
      rects[0].x = 0;
      rects[0].y = std::max(0, root_y - thickness / 2);
      rects[0].width =
          static_cast<unsigned short>(DisplayWidth(display, screen));
      rects[0].height = static_cast<unsigned short>(thickness);

      rects[1].x = std::max(0, root_x - thickness / 2);
      rects[1].y = 0;
      rects[1].width = static_cast<unsigned short>(thickness);
      rects[1].height =
          static_cast<unsigned short>(DisplayHeight(display, screen));

      // Update the window bounding shape so only these rectangles are visible.
      XserverRegion bounding = XFixesCreateRegion(display, rects, 2);
      XFixesSetWindowShapeRegion(display, window, ShapeBounding, 0, 0,
                                 bounding);
      XFixesDestroyRegion(display, bounding);

      // Clear the visible region and draw the thick white crosshair.
      XClearWindow(display, window);
      XFillRectangles(display, window, gc, rects, 2);

      // Draw 1px stippled (dashed) black lines centered on the same positions.
      // Horizontal: y = root_y
      XDrawLine(display, window, black_gc, 0, root_y,
                DisplayWidth(display, screen) - 1, root_y);
      // Vertical: x = root_x
      XDrawLine(display, window, black_gc, root_x, 0, root_x,
                DisplayHeight(display, screen) - 1);

      XFlush(display);

      // Keep the overlay on top: raise it after drawing so other apps can't
      // stay above.
      XRaiseWindow(display, window);
      XFlush(display);

      std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
  }

private:
  Display *display = nullptr;
  int screen = 0;
  Window root = 0;
  Window window = 0;
  GC gc = nullptr;
  GC black_gc = nullptr;
};

int main() {
  try {
    CrosshairOverlay overlay;
    overlay.run();
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}

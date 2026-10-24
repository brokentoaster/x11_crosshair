// selection_monitor.hpp
// Fetches X11 PRIMARY/CLIPBOARD selections in a background thread.
// Fetching can block or time out, so it runs on a separate connection so
// the overlay loop never blocks.
#pragma once
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <poll.h>
#include <string>
#include <thread>

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
  std::string fetchOne(Display *disp, Window win, Atom selection, Atom target, Atom prop) {
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
      const auto remaining =
          std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
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
      if (XGetWindowProperty(disp, win, prop, offset, 1024, False, AnyPropertyType, &actualType,
                             &actualFormat, &nitems, &bytesAfter, &propData) != Success) {
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
    Display *tDisp = XOpenDisplay(nullptr);
    if (!tDisp)
      return;

    // Create a hidden window to receive selection events
    Window tWin = XCreateSimpleWindow(tDisp, DefaultRootWindow(tDisp), 0, 0, 10, 10, 0, 0, 0);

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
      if (p.empty())
        p = fetchOne(tDisp, tWin, PRIMARY, XA_STRING, SEL_DATA);

      // 2. Try to fetch CLIPBOARD (Ctrl+C)
      c = fetchOne(tDisp, tWin, CLIPBOARD, UTF8_STRING, SEL_DATA);
      if (c.empty())
        c = fetchOne(tDisp, tWin, CLIPBOARD, XA_STRING, SEL_DATA);

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

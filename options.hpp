// options.hpp
// Command-line options, argument parsing, and the pure idle/fade state
// machine. Kept X-free so it can be unit tested.
#pragma once
#include <charconv>
#include <iostream>
#include <string>
#include <string_view>

struct Options {
  int pollIntervalMs = 0; // 0 = event-driven (XInput2)
  bool showSelection = true;
  int thickness = 3;
  int fadeMs = 0;         // 0 = never hide on idle
  int fadeDurationMs = 0; // 0 = hide abruptly; needs --fade-ms > 0
  std::string keyName;    // empty = key watching disabled
  bool keyInverted = false; // true = visible normally, hidden while held
  bool keyToggle = false;   // true = each press toggles visibility
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
     << "  --key NAME        Watch key NAME (e.g. Shift_L); crosshair visible"
     << " only while held (default Shift_L when watching is enabled)\n"
     << "  --key-mode MODE   normal (visible only while key held), inverted"
     << " (visible normally, hidden while held; default normal), or toggle"
     << " (each press switches the crosshair on or off; starts hidden)\n"
     << "  --help            Show this help\n"
     << "With no options the overlay is always visible (key watching is"
     << " opt-in via --key or --key-mode).\n";
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
    if (ec != std::errc() || ptr != sv.data() + sv.size() || result < 0) {
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
    } else if (arg == "--key") {
      if (i + 1 >= argc || argv[i + 1][0] == '\0') {
        std::cerr << "--key requires a keysym name\n";
        printUsage(argv[0], std::cerr);
        return 1;
      }
      opts.keyName = argv[++i];
    } else if (arg == "--key-mode") {
      if (i + 1 >= argc) {
        std::cerr << "--key-mode requires a value (normal|inverted|toggle)\n";
        printUsage(argv[0], std::cerr);
        return 1;
      }
      const std::string mode = argv[++i];
      if (mode == "normal") {
        opts.keyInverted = false;
        opts.keyToggle = false;
      } else if (mode == "inverted") {
        opts.keyInverted = true;
        opts.keyToggle = false;
      } else if (mode == "toggle") {
        opts.keyInverted = false;
        opts.keyToggle = true;
      } else {
        std::cerr << "Invalid --key-mode value: " << mode
                  << " (use normal, inverted, or toggle)\n";
        printUsage(argv[0], std::cerr);
        return 1;
      }
      // Any explicit --key-mode enables key watching with the default key.
      if (opts.keyName.empty()) {
        opts.keyName = "Shift_L";
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

// Edge-driven toggle latch for --key-mode toggle. A key press flips the
// latch; the release and any auto-repeat presses while the key is still
// down are ignored. The latch starts hidden and is X-free and testable.
struct ToggleLatch {
  bool visible = false; // starts hidden in toggle mode
  bool keyDown = false; // tracks the press edge to filter repeats
};

// Feed one raw-key state sample (keyPress = raw key currently down).
// Returns the latch value to use as keyHeld for evaluateVisibility.
inline bool updateToggleLatch(ToggleLatch &latch, bool keyPress) {
  if (keyPress) {
    if (!latch.keyDown) {
      latch.keyDown = true;
      latch.visible = !latch.visible;
    }
  } else {
    latch.keyDown = false;
  }
  return latch.visible;
}

// Pure idle/fade state machine; inputs in ms since epoch-like clock so it
// is testable without X.

struct IdleState {
  bool hideNow = false;
  bool fading = false;
  int alpha = 255;
};

// Idle/fade state machine for pointer inactivity; inputs in ms so it is
// testable without X.
static IdleState evaluateIdle(int fadeMs, int fadeDurationMs, long idleMsSinceMotion) {
  IdleState state;
  if (fadeMs <= 0 || idleMsSinceMotion < fadeMs) {
    return state;
  }
  const long idleMs = idleMsSinceMotion - fadeMs;
  if (fadeDurationMs > 0 && idleMs < fadeDurationMs) {
    state.fading = true;
    state.alpha =
        255 - static_cast<int>(255.0 * static_cast<double>(idleMs) /
                               static_cast<double>(fadeDurationMs));
    if (state.alpha < 0) {
      state.alpha = 0;
    }
  } else {
    state.hideNow = true;
  }
  return state;
}

// Combined visibility: hide when the key state says hide OR the idle state
// says hide (both sources must agree on show to render). keyWatching=false
// keeps today's always-visible behavior.
inline IdleState evaluateVisibility(int fadeMs, int fadeDurationMs,
                                    long idleMsSinceMotion, bool keyWatching,
                                    bool keyHeld, bool keyInverted) {
  const bool keyWantsVisible =
      !keyWatching || (keyHeld != keyInverted);
  IdleState state = evaluateIdle(fadeMs, fadeDurationMs, idleMsSinceMotion);
  if (!keyWantsVisible) {
    // Key suppression wins over any fade state.
    state.fading = false;
    state.hideNow = true;
  }
  return state;
}

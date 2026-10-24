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

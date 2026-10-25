# AGENTS.md

## Project

`x11_crosshair`: an X11 overlay that draws a click-through, always-on-top crosshair following the pointer, plus a display of the current PRIMARY/CLIPBOARD selection text. Plain C++20 against Xlib, XFIXES, and XInput2. No linter config; CI and unit tests exist (GitHub Actions, ctest).

## Layout

- `main.cpp` — overlay window setup, event/poll loop, drawing.
- `options.hpp` — `Options`, argument parsing (`parseArgs`), and the pure idle/fade state machine (`evaluateIdle`); X-free so it is unit-testable.
- `selection_monitor.hpp` — `SelectionMonitor` background thread with its own X connection.
- `tests/test_logic.cpp` — assert-harness tests for parsing and idle state (no X needed).
- `.clang-format` — LLVM-based, 2-space indent, 100 columns; run `clang-format -i` on touched sources.

## Build

```sh
mkdir -p build && cd build
cmake ..
make            # builds x11_crosshair (and tests by default)
ctest           # runs the logic tests
./x11_crosshair # run from the build dir; stop with pkill x11_crosshair
```

CI (`.github/workflows/ci.yml`) builds with `-Wconversion` + `-Werror` and runs the tests, plus an ASan/UBSan job.

Requires `libx11-dev`, `libxfixes-dev`, `libxi-dev`, `pkg-config`, CMake ≥ 3.13, C++20 compiler.

## Runtime options

A single `main.cpp` implements all modes, selected by command-line flags (see README.md for the user-facing table): `--poll [MS]` switches to XQueryPointer polling (default 10 ms interval, skips all XI2 setup), `--no-selection` disables the text box and skips starting `SelectionMonitor`, `--thickness N` sets the crosshair line width (default 3), `--fade-ms N` hides the overlay after N ms without pointer motion (0 = off), and `--fade-duration MS` soft-fades out after that timeout. `--key NAME`/`--key-mode normal|inverted` (opt-in; passing either enables watching) tie visibility to a held key: normal = visible only while held, inverted = hidden while held. No-argument defaults reproduce the original behavior exactly.

## Event loop and selection architecture

- Click-through is achieved with `XFixesSetWindowShapeRegion` on `ShapeInput` set to an empty region; visibility (only the crosshair pixels) via `ShapeBounding`. Both must be set for correct behavior. Idle hiding (`--fade-ms`) also hides by clearing `ShapeBounding` — never unmap the window to hide it.
- The overlay window is `override_redirect` (no WM decorations) and is re-raised each frame; a 32-bit TrueColor visual + matching colormap is requested when available.
- Selection fetching can block (other clients own selections); it must stay in the `SelectionMonitor` background thread with a poll-based 100 ms timeout per fetch (UTF8_STRING first, XA_STRING fallback), re-polling both PRIMARY and CLIPBOARD roughly every 200 ms via its own display connection — never move it onto the render/event thread.
- Sanitization conventions for displayed selection text: newlines → spaces, truncate at 50 chars with `...`.
- Event-driven motion: the main target selects `XI_RawMotion` on the root window via XInput2 (`XISelectEvents`); the loop waits on the connection (up to 250 ms safety net, or until the idle deadline when `--fade-ms` is active) and renders on motion. Keep this design (it exists to reduce CPU vs polling).
- Key watching (`--key`/`--key-mode`) uses XI2 raw key events (`XI_RawKeyPress`/`XI_RawKeyRelease`) selected on the root window. Raw events are broadcast to all selecting clients and consume nothing — never use `XGrabKey`/`XGrabKeyboard`, which would steal keys from other apps. Visibility combines as hide-if-either-source-says-hide (`evaluateVisibility` in options.hpp, unit-tested); key transitions force a render bypass of the change guard. The XI2 event mask buffer must stay sized for the largest selected event (`XIMaskLen(XI_RawMotion)`; motion is event 17 = 3 bytes).
- Xlib calls are buffered; `XFlush` after map/raise/draw is required for responsiveness.
- Idle hiding: the run loop tracks last pointer motion; once idle past `--fade-ms` it clears `ShapeBounding` and skips drawing until the next motion. `--fade-duration` ramps the drawing GC foreground alpha (ARGB, `alpha << 24 | rgb`) down over the duration before hiding; visible only with the 32-bit visual + compositor, elsewhere the abrupt hide remains.

## Style

- C++20, compile flags `-Wall -Wextra -Wpedantic -Wconversion` (CI enforces with `-Werror`; keep warning-free locally too).
- Header/module split above; keep pure logic (parsing, idle state, layout math) X-free and covered by tests. No external deps beyond X11/XFIXES/XI.
- Naming: PascalCase classes, camelCase members/functions.
- Specs and implementation plans live in `docs/workplans/` (`*-Spec.md` / `*-Plan.md` pairs).
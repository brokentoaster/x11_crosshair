# AGENTS.md

## Project

`x11_crosshair`: an X11 overlay that draws a click-through, always-on-top crosshair following the pointer, plus a display of the current PRIMARY/CLIPBOARD selection text. Plain C++20 against Xlib, XFIXES, and XInput2. No tests, no CI, no linter config.

## Build

```sh
mkdir -p build && cd build
cmake ..
make            # builds x11_crosshair
./x11_crosshair # run from the build dir; stop with pkill x11_crosshair
```

Requires `libx11-dev`, `libxfixes-dev`, `libxi-dev`, `pkg-config`, CMake ≥ 3.13, C++20 compiler.

## Runtime options

A single `main.cpp` implements all modes, selected by command-line flags (see README.md for the user-facing table): `--poll [MS]` switches to XQueryPointer polling (default 10 ms interval, skips all XI2 setup), `--no-selection` disables the text box and skips starting `SelectionMonitor`, `--thickness N` sets the crosshair line width (default 3). No-argument defaults reproduce the original behavior exactly.

## Event loop and selection architecture

- Click-through is achieved with `XFixesSetWindowShapeRegion` on `ShapeInput` set to an empty region; visibility (only the crosshair pixels) via `ShapeBounding`. Both must be set for correct behavior.
- The overlay window is `override_redirect` (no WM decorations) and is re-raised each frame; a 32-bit TrueColor visual + matching colormap is requested when available.
- Selection fetching can block (other clients own selections); it must stay in the `SelectionMonitor` background thread with a poll-based 100 ms timeout per fetch (UTF8_STRING first, XA_STRING fallback), re-polling both PRIMARY and CLIPBOARD roughly every 200 ms via its own display connection — never move it onto the render/event thread.
- Sanitization conventions for displayed selection text: newlines → spaces, truncate at 50 chars with `...`.
- Event-driven motion: the main target selects `XI_RawMotion` on the root window via XInput2 (`XISelectEvents`), received through `XNextEvent` + cookie retrieval. Keep this design (it exists to reduce CPU vs polling).
- Xlib calls are buffered; `XFlush` after map/raise/draw is required for responsiveness.

## Style

- C++20, compile flags `-Wall -Wextra -Wpedantic` (keep warning-free).
- Single-file implementation, no external deps beyond X11/XFIXES/XI. Prefer adding to the existing file over introducing new headers/libraries.
- Naming: PascalCase classes, camelCase members/functions.
- Specs and implementation plans live in `docs/workplans/` (`*-Spec.md` / `*-Plan.md` pairs).
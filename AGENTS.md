# AGENTS.md

## Project

`x11_crosshair`: an X11 overlay that draws a click-through, always-on-top crosshair following the pointer, plus a display of the current PRIMARY/CLIPBOARD selection text. Plain C++20 against Xlib, XFIXES, and XInput2. No tests, no CI, no linter config.

## Build

```sh
mkdir -p build && cd build
cmake ..
make            # builds x11_crosshair and x11_crosshair2
./x11_crosshair # run from the build dir; stop with pkill x11_crosshair
```

Requires `libx11-dev`, `libxfixes-dev`, `libxi-dev`, `pkg-config`, CMake ≥ 3.13, C++20 compiler.

## Multiple entry points (important)

There are three `main*` files; only two are built, and they are alternative implementations of the same idea, not parts of one program:

- `main_sel.cpp` → target `x11_crosshair`: the current/primary implementation. Event-driven (XInput2 raw motion events to reduce CPU), and displays the current selection text via a background `SelectionMonitor` thread using a **separate X display connection** for selection fetching so the overlay loop never blocks.
- `main2.cpp` → target `x11_crosshair2`: older polling-based version. `main2.cpp` is a 7-line wrapper instantiating the generated `CrosshairOverlay` class, which does all the work (XQueryPointer each frame, 10 ms sleep). Note `x11_crosshair2` builds with `-fno-exceptions -fno-rtti` and aggressive optimization flags, unlike the main target.
- `main.cpp`: standalone single-file polling version; **not referenced by CMakeLists.txt**.

When changing behavior, edit `main_sel.cpp` first; the others are historical/experimental.

## Generated code gotcha

`CrosshairOverlay.h` and `CrosshairOverlay.cpp` are **machine-generated** by `gen01.lisp`, which loads `util.lisp` and is a Common Lisp program using `cl-cpp-generator2` (requires Quicklisp + cl-change-case). Do not hand-edit these two files for style or structure; regenerate instead or hand-edit only with the understanding that `gen01.lisp` will overwrite them. The generator writes only when the emitted source hash changed and pipes through `clang-format`. The lisp files contain an absolute path (`/home/kiel/stage/x11_crosshair/`) that must match the checkout location when regenerating.

## X11 patterns to preserve

- Click-through is achieved with `XFixesSetWindowShapeRegion` on `ShapeInput` set to an empty region; visibility (only the crosshair pixels) via `ShapeBounding`. Both must be set for correct behavior.
- The overlay window is `override_redirect` (no WM decorations) and is re-raised each frame; a 32-bit TrueColor visual + matching colormap is requested when available.
- Selection fetching can block (other clients own selections); it must stay in the `SelectionMonitor` background thread with a poll-based 100 ms timeout per fetch (UTF8_STRING first, XA_STRING fallback), re-polling both PRIMARY and CLIPBOARD roughly every 200 ms via its own display connection — never move it onto the render/event thread.
- Sanitization conventions for displayed selection text: newlines → spaces, truncate at 50 chars with `...`.
- Event-driven motion: the main target selects `XI_RawMotion` on the root window via XInput2 (`XISelectEvents`), received through `XNextEvent` + cookie retrieval. Keep this design (it exists to reduce CPU vs polling).
- Xlib calls are buffered; `XFlush` after map/raise/draw is required for responsiveness.

## Style

- C++20, compile flags `-Wall -Wextra -Wpedantic` (keep warning-free). Note README.md says C++17; trust CMakeLists.txt.
- Single-file implementations, no external deps beyond X11/XFIXES/XI. Prefer adding to the existing file over introducing new headers/libraries.
- Naming: PascalCase classes, camelCase members/functions, matching the existing files.
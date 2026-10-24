# TBC-consolidate-crosshair: Single executable with runtime-selected modes

## Goal

`x11_crosshair` builds as one executable from one codebase. All operating
behaviors that are currently split across three `main*` files (event-driven
XInput2 vs. polling; selection display) become command-line arguments, so a
user can pick a mode at runtime without rebuilding. The repository no longer
contains duplicate or machine-generated implementations of the overlay.

## Context

Today the repo has three overlapping implementations:

- `main_sel.cpp` → target `x11_crosshair`: primary implementation; XInput2
  raw-motion event loop plus a background `SelectionMonitor` thread that
  fetches PRIMARY/CLIPBOARD text on a separate X display connection.
- `main2.cpp` + generated `CrosshairOverlay.h/.cpp` → target
  `x11_crosshair2`: polling variant (XQueryPointer each frame, 10 ms sleep).
- `main.cpp`: standalone single-file polling variant, not built.

The variants differ mainly in how the pointer is tracked (events vs. polling)
and in what is drawn (selection text box vs. crosshair only). This makes
behavior changes error-prone: fixes must be applied in up to three places, and
two of the variants are either generated (`gen01.lisp` output) or stale.

## Scope

### In scope

- One `main.cpp` (replacing `main_sel.cpp`) containing a single
  `CrosshairOverlay` implementation supporting, at runtime:
  - Pointer tracking mode: XInput2 raw-motion (default) or polling, selected
    via a flag (e.g. `--poll [interval_ms]`, default interval 10 ms).
  - Selection display on/off via a flag (e.g. `--selection`, on by default to
    preserve current primary behavior).
  - Crosshair thickness via a flag (default 3, matching current value).
- A minimal argument parser in the same file (no external dependency such as
  `cxxopts`); long options only, `--help` printing usage and defaults.
- CMakeLists.txt producing a single target `x11_crosshair` with the current
  warning flags (`-Wall -Wextra -Wpedantic`) and the existing Release
  optimization block.
- Deletion of `main2.cpp`, `CrosshairOverlay.h/.cpp`, the old `main.cpp`
  contents, and the second CMake target. `gen01.lisp`/`util.lisp` deletion is
  optional; see Open Questions.
- README.md and AGENTS.md updated to describe the new flags and the
  single-target build.

### Out of scope

- Any change to drawing behavior, sanitization rules, shape/click-through
  mechanics, or the background-thread selection design beyond moving it under
  a flag.
- XWayland/Wayland support, font selection, or new features (color options,
  shaped crosshair styles).
- Config files or environment-variable configuration; arguments only.
- Long/short option aliases beyond what is listed in the interface below.

## Constraints & Assumptions

- X11 invariants from AGENTS.md must be preserved: `ShapeInput` empty region
  for click-through, `ShapeBounding` for visibility, `override_redirect`
  window, `XFlush` after map/raise/draw, selection fetching only on the
  background thread with a poll-based ~100 ms timeout on its own display
  connection.
- C++20, Xlib/XFIXES/XI only; keep the single-file layout (one `.cpp`).
- Defaults must reproduce today's `x11_crosshair` behavior exactly, so
  existing users running the binary with no arguments see no change.
- No tests exist in the repo and none are added; verification is manual.

## Decisions

- Argument parsing is hand-rolled (small option set; avoids adding a
  dependency against the repo's no-external-deps convention).
- Polling mode is reimplemented inside the single file (it is ~20 lines:
  XQueryPointer + sleep) rather than porting the generated
  `CrosshairOverlay` code.
- Option names: `--poll [MS]`, `--no-selection`, `--thickness N`,
  `--help`. `--poll` without a value uses the historical 10 ms interval.

## Acceptance Criteria

1. `cmake .. && make` in `build/` builds exactly one target, `x11_crosshair`,
   warning-free under `-Wall -Wextra -Wpedantic`, and no `main2`,
   `x11_crosshair2`, or generated `CrosshairOverlay.*` sources remain in the
   build graph.
2. Running `./x11_crosshair` with no arguments shows the crosshair following
   the pointer via XInput2 events and the selection text box, identical to the
   current `x11_crosshair` behavior.
3. Running `./x11_crosshair --poll` tracks the pointer via XQueryPointer
   polling at 10 ms; `--poll 50` polls every 50 ms (verifiable by observing
   reduced responsiveness and CPU in top).
4. Running `./x11_crosshair --no-selection` draws the crosshair only; no text
   box appears when text is selected or copied, and the `SelectionMonitor`
   thread is not started (no extra X display connection; checkable with
   `xwininfo -root -tree` or by code review).
5. Running `./x11_crosshair --thickness 7` draws a 7 px crosshair.
6. `./x11_crosshair --help` prints option list and defaults to stdout and
   exits 0; an unknown option prints usage to stderr and exits non-zero.
7. In polling mode the overlay remains click-through (input passes to
   underlying windows) and stays on top, matching event mode.
8. README.md and AGENTS.md describe the single target and the new flags; no
   references to `x11_crosshair2` or generated code remain in them.

## Verification Plan

- Build clean with warnings enabled; confirm only one target via
  `cmake --build build --target help` (or inspecting `make help`).
- Manual X11 session checks for AC 2–7: run each flag combination, select and
  copy text in another app, move pointer across light/dark backgrounds, click
  through the overlay into a terminal, and confirm stacking behavior over a
  normal window.
- Code review against AGENTS.md X11 invariants (shape regions, override
  redirect, XFlush placement, background selection thread).

## Rollback Plan

- Revert the single commit (or commit series) restoring `main_sel.cpp`,
  `main2.cpp`, `CrosshairOverlay.*`, and the two CMake targets. No data,
  config, or external state is affected.

## Open Questions

- Should `gen01.lisp`/`util.lisp` be deleted along with the generated sources,
  or kept as historical generator material? Default plan: delete the generated
  `.h/.cpp`, keep the lisp files until the new single-file implementation is
  confirmed working, then remove them.
- Should `--selection` be an explicit opt-in instead of `--no-selection`
  opt-out? Default plan keeps selection on by default (current behavior).
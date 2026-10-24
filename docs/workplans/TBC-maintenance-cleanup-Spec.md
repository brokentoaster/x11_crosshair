# TBC-maintenance-cleanup: Fix correctness bugs and structure the codebase — Spec

## Goal

The overlay behaves correctly in edge cases (selections longer than 1 KB,
non-terminating property data, invalid numeric arguments, GC color setup with
the 32-bit visual, resource cleanup on failure), and the code is organized so
each concern (selection fetching, idle/fade state, layout, drawing) lives in
a named, individually reviewable unit. Tooling (format config, CI with
warnings-as-errors, unit tests for pure logic) keeps it that way.

## Context

The consolidation and feature work left `main.cpp` at ~560 lines with a
200-line `run()` that mixes event waiting, the idle/fade state machine, text
layout, shape updates, and drawing. Review found latent bugs: property data
overread and truncation in `fetchOne`, missing colormap free, no exception
safety in `CrosshairOverlay`'s constructor, `atoi` silently accepting garbage
arguments, and disagreeing color sources (`WhitePixel` vs. explicit ARGB) on
the 32-bit visual. The XI2 event path fetches cookie data it then discards
while still polling `XQueryPointer` every 16 ms, so its complexity buys
nothing in its current form. There are no tests, no CI, and no formatter
config, and the file mixes indentation styles.

## Scope

### In scope

- Correctness fixes:
  - `fetchOne` reads the property with proper length handling (loop while
    `bytesAfter > 0`, or size the string by `nitems`) — no overread, no 1 KB
    truncation.
  - Argument parsing validates full numeric values (reject `12abc` and
    empty strings) with `std::from_chars` or equivalent.
  - `CrosshairOverlay` constructor/destructor are leak-free on all paths:
    colormap freed in destructor, partial-failure cleanup on constructor
    error (order allocations or use local RAII helpers).
  - GC colors unified: when the 32-bit ARGB visual is active, full-opacity
    colors use explicit ARGB constants (`0xFFFFFF`/`0x000000`); default
    visual keeps `WhitePixel`/`BlackPixel`. The fade path already uses ARGB
    and must agree.
- Structure refactor (behavior-preserving):
  - `run()` decomposed into named private methods: wait for wakeup (event or
    poll), evaluate idle/fade state (pure function of position/time inputs),
    compute text layout, apply bounding shape, draw frame. The idle state
    machine becomes a small pure struct/function that can be unit-tested
    without X.
  - Selection fetching moves to `selection_monitor.hpp` (header-only
    split is acceptable to preserve the CMake single-target layout);
    stray includes hoisted to the top.
  - XI2 decision: either make XI2 the real wake source (event-driven wait,
    no 16 ms polling round-trip in event mode) or remove XI2 and keep
    polling. Chosen direction recorded in Decisions.
- Tooling:
  - `.clang-format` (project style: 2-space indent, ~100 col) and one
    formatting pass over the sources; no logic changes in that commit.
  - CMake option `BUILD_TESTS` (default ON) with a small test target for
    pure logic: argument parsing (via a testable seam) and the idle state
    machine. No X server dependency in tests.
  - GitHub Actions workflow: install X11/XFIXES/XI dev packages, configure,
    build with `-Wall -Wextra -Wpedantic -Werror`, run tests.
  - `-Wconversion` adopted for the main target after a one-time cleanup of
    the narrowing casts.
- Docs updated: README/AGENTS.md notes on structure and CI; AGENTS.md
  retains the X11 invariants.

### Out of scope

- Selection-monitor event-driven rewrite via `XFixesSelectSelectionInput`
  (behavior change; separate spec if wanted).
- `INCR`/`MULTIPLE`/`TIMESTAMP` selection-target support beyond fixing the
  read loop.
- RandR/multi-monitor resize handling, graceful shutdown/signals, logging.
- Wayland support, new user-facing features, flag changes.
- Formatting other historical files (only files that remain in the build).

## Constraints & Assumptions

- X11 invariants from AGENTS.md preserved (shape-only hiding, override
  redirect, XFlush placement, selection on background thread with own
  display connection).
- Refactor commits must be behavior-preserving; correctness fixes are
  separate commits so a regression can be bisected.
- No new external dependencies (tests use a minimal self-contained
  assertion approach, e.g. a tiny `assert`-based main, not gtest).
- Manual X11 verification still needed for rendering behavior; tests cover
  only pure logic.

## Decisions

- XI2: keep the extension but make it the actual wake source in event mode —
  wait on the connection with no fixed timeout; use `XQueryPointer` only
  when `--poll` is set. Rationale: event mode already selects XI_RawMotion;
  this removes the 16 ms busy-cycle and makes the mode honest. If the
  refactor reveals the wait cannot be made correct simply, fall back to
  removing XI2 entirely (simpler, still correct) and record the swap in the
  plan.
- Header-only split (`selection_monitor.hpp`, `overlay.hpp`) rather than a
  multi-target CMake layout: keeps CMakeLists minimal while ending the
  god-file.
- Tests via a self-contained macro-assert harness to honor the
  no-dependencies convention.

## Acceptance Criteria

1. `fetchOne` correctly returns selections longer than 1 KB (test by
   selecting >1 KB text) and never reads past `nitems` bytes (code review;
   construct string with explicit length).
2. `./x11_crosshair --fade-ms 12abc`, `--thickness ""`, and similar invalid
   values print an error, show usage, and exit non-zero; valid values behave
   as before.
3. Killing the process after intentional constructor failure (e.g.
   simulated `XCreateWindow` failure via review or fault-injection build
   flag) leaks nothing per valgrind/ASAN review — colormap, GCs, window all
   freed in destructor.
4. With the 32-bit visual active, the crosshair is white/black as before
   (visually unchanged) and the fade path and full-opacity path use the
   same color source.
5. Event mode wakes on pointer motion without a fixed 16 ms cycle (CPU in
   `top` drops versus previous event mode while idle-visible) and still
   re-renders within one frame of motion; `--poll` mode unchanged.
6. Overlay behavior (crosshair, selection box, `--fade-ms`,
   `--fade-duration`) is unchanged after the refactor commit series apart
   from the XI2 wake change in AC 5.
7. `make test` (or `ctest`) builds and passes the option-parsing and
   idle-state tests; tests touch no X server.
8. CI workflow builds the project with `-Werror` (including
   `-Wconversion`) and runs the tests on a runner; the repo has
   `.clang-format` and sources are formatted with it.
9. README.md and AGENTS.md reflect the new file layout and CI; no stale
   "single-file" claims remain.

## Verification Plan

- Tests for AC 2, 7 via the new test target; run in CI (AC 8).
- Manual X11 checks for AC 1, 4, 5, 6: select >1 KB text and confirm display
  (first 50 chars), visual comparison before/after color unification, CPU
  comparison in top for idle event mode, and a flag-by-flag smoke run of all
  modes after refactor.
- ASAN/valgrind run (local, optional CI job) for AC 3.
- Code review against AGENTS.md invariants for every commit.

## Rollback Plan

- Each commit is independently revertible (fixes, refactor, tooling are
  separate series). Reverting the refactor series restores the prior
  structure; reverting the fixes restores prior (buggy) behavior with no
  data impact.

## Open Questions

- Exact test-target naming and whether tests live in `tests/` (proposal:
  `tests/test_logic.cpp`, target `x11_crosshair_tests`, wired via
  `BUILD_TESTS`).
- Whether the CI also runs ASAN (optional job; proposal: yes, ubuntu-latest,
  one extra step).
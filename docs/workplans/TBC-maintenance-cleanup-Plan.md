# TBC-maintenance-cleanup: Fix correctness bugs and structure the codebase — Plan

## Spec
- Spec: docs/workplans/TBC-maintenance-cleanup-Spec.md
- Jira: n/a

## Overview
- Outcome: correctness bugs fixed in isolation, `main.cpp` decomposed into
  named units with a pure-testable idle state machine, and the repo gains a
  formatter config, unit tests for pure logic, and CI with `-Werror`.
- Approach: fix bugs first (small, individually revertible commits), then a
  behavior-preserving structure series (split `run()`, split
  `selection_monitor.hpp`, resolve XI2 wake design), then tooling
  (`.clang-format`, tests, CI). Formatting pass last so diffs stay reviewable.

## Tasks
- [x] Task 1: Fix selection property reads in `fetchOne`
  - Notes: build the result string from `propData` with explicit `nitems`
    length; loop the `XGetWindowProperty` call while `bytesAfter > 0`
    (advancing offset) so >1 KB selections read fully. Delete the property
    after the loop.
  - Touchpoints: `main.cpp` (`SelectionMonitor::fetchOne`)
  - Done when: spec AC 1 — manual >1 KB selection test and code review.
- [x] Task 2: Strict numeric argument validation
  - Notes: replace `atoi` with `std::from_chars` (include `<charconv>`),
    rejecting trailing characters and empty strings for `--poll`, `--fade-ms`,
    `--fade-duration`, `--thickness`. Keep error/usage/exit-code shape.
    Factor parsing into a small function usable from tests.
  - Touchpoints: `main.cpp` (`parseArgs`, `Options`)
  - Done when: spec AC 2 — invalid values rejected; valid unchanged.
- [x] Task 3: Resource-leak fixes in `CrosshairOverlay`
  - Notes: free the colormap in the destructor; on constructor error paths
    clean up already-allocated resources (either order allocations so
    failures precede allocations, or add tiny RAII guards locally). Keep
    behavior identical on the success path.
  - Touchpoints: `main.cpp` (`CrosshairOverlay` ctor/dtor)
  - Done when: spec AC 3 — ASAN/valgrind local run clean on startup/exit.
- [x] Task 4: Unify crosshair color source on the 32-bit visual
  - Notes: capture whether the ARGB visual was chosen; when true use
    `0xFFFFFF`/`0x000000` explicit pixels for full-opacity (matching the fade
    path), else keep `WhitePixel`/`BlackPixel`. One helper (e.g.
    `fullOpacityWhite()/fullOpacityBlack()`) used by both paths.
  - Touchpoints: `main.cpp` (ctor GC setup, draw section of `run()`)
  - Done when: spec AC 4 — visual comparison before/after; fade still works.
- [x] Task 5: Decompose `run()` into named phases
  - Notes: extract `waitForWakeup()` (poll/event wait + XI2 drain),
    `evaluateIdle()` (pure function: inputs `positionChanged`, `nowMs`,
    `lastMotionMs`, `options` → `IdleState{hidden, fading, alpha}`), text
    layout (`computeTextBox()`), shape application (`applyBoundingShape()`),
    and `drawFrame()`. `run()` becomes a short driver. Behavior-preserving:
    same state transitions, same guard ordering. `evaluateIdle` must not
    touch X so Task 7 can test it.
  - Touchpoints: `main.cpp` (`run()` and new private methods)
  - Done when: spec AC 6 — manual smoke run of all flag combinations;
    no logic edits beyond moving code.
- [x] Task 6: Resolve XI2 wake design
  - Notes: per spec Decisions, make event mode wait on the connection until
    an event arrives (poll with timeout recomputed from idle-deadline when
    `--fade-ms` is active so hidden state still wakes to hide), render on
    XI_RawMotion, keep `XQueryPointer` for absolute coords. If the wait
    cannot be made correct simply (e.g. selection box must update while
    visible-idle), fall back to removing XI2 entirely and note the swap.
    Polling mode untouched.
  - Touchpoints: `main.cpp` (wait phase from Task 5)
  - Done when: spec AC 5 — CPU drop while idle-visible in `top`, motion
    still renders within a frame.
- [x] Task 7: Split `selection_monitor.hpp`, hoist includes
  - Notes: move `SelectionMonitor` into `selection_monitor.hpp` (header-only,
    included by `main.cpp`); move the stray include block to the top of
    `main.cpp`; sort includes. No logic changes.
  - Touchpoints: `main.cpp`, new `selection_monitor.hpp`, `CMakeLists.txt`
    (no target change needed for header-only)
  - Done when: clean build, all modes still work (spec AC 6).
- [x] Task 8: Add `.clang-format` and one formatting pass
  - Notes: style per spec (2-space indent, ~100 col, LLVM-based basis);
    format only `main.cpp` and `selection_monitor.hpp`; verify no warnings
    and a smoke run — formatting commit must contain no logic changes.
  - Touchpoints: `.clang-format` (new), sources
  - Done when: diff review shows whitespace-only changes; build + smoke run.
- [x] Task 9: Unit tests for pure logic
  - Notes: `tests/test_logic.cpp` with a self-contained assert harness
    covering: option parsing (valid/invalid values, unknown flag, help path
    via a seam — pass `argc/argv` vectors and check `Options` + return code)
    and `evaluateIdle` (timeout boundary, fade alpha ramp, hide transition,
    re-show on motion). CMake: `option(BUILD_TESTS "Build tests" ON)` +
    `enable_testing()` + `add_test` when ON.
  - Touchpoints: `tests/test_logic.cpp` (new), `CMakeLists.txt`, possibly
    exposing a header for `parseArgs`/`evaluateIdle` (e.g. `options.hpp`)
  - Done when: spec AC 7 — `ctest` passes locally with no X server running.
- [x] Task 10: CI workflow
  - Notes: `.github/workflows/ci.yml` — ubuntu-latest, apt install
    `libx11-dev libxfixes-dev libxi-dev pkg-config`, configure, build with
    `-DCMAKE_CXX_FLAGS="-Wconversion"` and warnings-as-errors, `ctest`.
    Optional ASAN job per spec Open Questions (default: include it).
  - Touchpoints: `.github/workflows/ci.yml` (new), `CMakeLists.txt` (ASAN
    option if added)
  - Done when: spec AC 8 — workflow green on the branch.
- [x] Task 11: Update docs, tick plan
  - Notes: README — build/test section gains `ctest`, note CI. AGENTS.md —
    replace "single-file implementation" with the new layout
    (`main.cpp`, `selection_monitor.hpp`, `tests/`), keep X11 invariants,
    record the XI2 wake outcome. Run the markdown table formatter on edited
    tables.
  - Touchpoints: `README.md`, `AGENTS.md`
  - Done when: spec AC 9.

## Dependencies & prerequisites
- Tasks 1–4 (fixes) are independent of Tasks 5–7 (refactor); do fixes first
  so refactor diffs stay behavior-focused.
- Task 5 before Task 6 (wake design lands in the extracted wait phase).
- Task 7 after Task 5 (split a stable structure, not a moving one).
- Task 8 (format) before Task 9–10 so tests/CI review clean diffs; Task 9
  before Task 10 (CI runs the tests).
- Task 11 last.

## Verification
- See spec Acceptance Criteria and Verification Plan.
- After the whole series: full flag smoke run (`--poll`, `--no-selection`,
  `--thickness`, `--fade-ms`, `--fade-duration`, `--help`) and a `ctest` run
  as regression gate.

## Risks & mitigations
- Risk: making XI2 the wake source changes render latency subtly (e.g.
  coalesced motion events) → Mitigate: compare motion-to-render visually and
  via a simple timestamp log before/after; the spec explicitly allows
  falling back to removing XI2.
- Risk: `evaluateIdle` extraction changes state-transition ordering (the
  hidden/fading interplay is subtle) → Mitigate: Task 5 is
  behavior-preserving by review, then Task 9 pins the transitions in tests
  before any further change.
- Risk: `-Wconversion` surfaces many pre-existing warnings → Mitigate: scoped
  to the main target; clean the few casts in Task 4/5 commits before
  enabling it in CI.
- Open: whether `XGetWindowProperty` loop needs `Xutf8` variants for
  multi-byte safety — verify with real UTF-8 selections during Task 1
  testing.

## Out of scope
- Selection event-driven monitor, INCR/MULTIPLE/TIMESTAMP, RandR, signals,
  logging, new features (spec Out of scope).

## Rollback
- See spec Rollback Plan (commit-series reverts; fixes, refactor, tooling
  are separate series).
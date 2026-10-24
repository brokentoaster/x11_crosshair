# TBC-consolidate-crosshair: Single executable with runtime-selected modes — Plan

## Spec
- Spec: docs/workplans/TBC-consolidate-crosshair-Spec.md
- Jira: n/a

## Overview
- Outcome: one `x11_crosshair` target built from one consolidated `main.cpp`,
  with pointer-tracking mode, selection display, and crosshair thickness
  chosen by command-line flags; duplicate mains and generated code removed.
- Approach: evolve `main_sel.cpp` into the single file (it already has both
  the event loop and the selection monitor; add a small `Options` struct and
  hand-rolled parser at the top). Reimplement polling as a trivial fallback
  inside the same run loop. Delete other sources and the second CMake target
  last, so the build stays green throughout.

## Tasks
- [x] Task 1: Add option parsing and Options struct to `main_sel.cpp`
  - Notes: struct fields `pollIntervalMs` (0 = event mode), `showSelection`
    (true), `thickness` (3). Parse `--poll [MS]`, `--no-selection`,
    `--thickness N`, `--help`; unknown option → usage on stderr, exit 1
    (spec AC 6). Thread `Options` into `CrosshairOverlay` and `run()`
    (replaces hardcoded `thickness`/`min_interval`; `min_interval` still
    applies in event mode).
  - Touchpoints: `main_sel.cpp`
  - Done when: `./x11_crosshair --help` prints options/defaults and exits 0;
    no-arg run behaves as today (spec AC 2, 6).
- [x] Task 2: Implement polling tracking mode inside the same run loop
  - Notes: when `pollIntervalMs > 0`, skip the XI2/XPending wait and drive the
    loop with `XQueryPointer` + sleep(pollIntervalMs); skip `XISelectEvents`
    setup in the constructor in this mode. Preserve the redraw-on-change
    guard and all shape/XFlush behavior.
  - Touchpoints: `main_sel.cpp`
  - Done when: `--poll` and `--poll 50` track the pointer at the requested
    cadence; overlay stays click-through and on top (spec AC 3, 7).
- [x] Task 3: Make selection display conditional
  - Notes: construct `SelectionMonitor` only when `showSelection` is true;
    when false, build `lines` empty so no text box rect is ever emitted.
    Verify no second display connection is opened (spec AC 4).
  - Touchpoints: `main_sel.cpp` (`SelectionMonitor` usage in `run()`)
  - Done when: `--no-selection` shows crosshair only (spec AC 4).
- [x] Task 4: Make crosshair thickness configurable
  - Notes: replace the hardcoded `const int thickness = 3` with
    `options.thickness`; watch the `unsigned short` casts and the
    `root_x - thickness / 2` centering math for N=1.
  - Touchpoints: `main_sel.cpp`
  - Done when: `--thickness 7` draws a 7 px crosshair; `--thickness 1` stays
    centered and warning-free (spec AC 5).
- [x] Task 5: Consolidate to a single CMake target and delete duplicates
  - Notes: rename `main_sel.cpp` → `main.cpp` (replacing old `main.cpp`),
    drop the `x11_crosshair2` target and its special flags from
    CMakeLists.txt, delete `main2.cpp`, `CrosshairOverlay.h/.cpp`, and
    `gen01.lisp`/`util.lisp` (spec Open Question default: delete once Task 2
    is confirmed working). Keep the GNU Release optimization block on the
    single target.
  - Touchpoints: `CMakeLists.txt`, `main.cpp`, repo root
  - Done when: only one target exists, `make` builds warning-free, no stale
    sources remain (spec AC 1).
- [x] Task 6: Update README.md and AGENTS.md
  - Notes: README — single build/run section, new flags table, remove
    x11_crosshair2/generated-code references and the stale C++17 note.
    AGENTS.md — remove "multiple entry points" and "generated code" sections,
    document the flags and single-file layout.
  - Touchpoints: `README.md`, `AGENTS.md`
  - Done when: no references to `x11_crosshair2` or generated code remain
    (spec AC 8).

## Dependencies & prerequisites
- Tasks 1–4 are serial edits to the same file; do them in order.
- Task 5 requires Tasks 1–4 done (poll mode proven before deleting the
  generated polling implementation).
- Task 6 after Task 5 (docs must describe the final tree).

## Verification
- See spec Acceptance Criteria and Verification Plan.
- Build-time check for AC 1: `cmake .. && make` in `build/` shows only the
  `x11_crosshair` target; rebuild from a clean `build/` dir after Task 5.

## Risks & mitigations
- Risk: polling and event modes share the redraw path; a flag combination
  could double-drive the loop → Mitigate: keep a single render body reached by
  two wait strategies; manually test both modes plus every flag combination
  (spec Verification Plan).
- Risk: `XISelectEvents` still set in poll mode wastes events → Mitigate:
  skip XI2 setup entirely when `pollIntervalMs > 0`.

## Out of scope
- Wayland/XWayland, fonts, colors, config files, new features, argument
  aliases beyond the four flags (see spec Out of scope).

## Rollback
- See spec Rollback Plan (revert the commit series).
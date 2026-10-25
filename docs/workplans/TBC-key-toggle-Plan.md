# TBC-key-toggle: Show/hide crosshair on key press — Plan

## Spec
- Spec: docs/workplans/TBC-key-toggle-Spec.md
- Jira: n/a

## Overview
- Outcome: with `--key`/`--key-mode` passed, crosshair (and selection box)
  visibility follows the held state of the selected key — normal mode shows
  only while held, inverted mode hides while held. No grabs; other clients
  are unaffected. No-argument behavior unchanged.
- Approach: mirror the existing XI2 raw-motion pattern — select
  `XI_RawKeyPress`/`XI_RawKeyUp` on the root window, track held state as a
  bool (repeat-immune by ignoring raw-key events for the tracked state
  transitions), fold it into `evaluateIdle` as a visibility input. The run
  loop's wake logic must also wake on key events, not just motion.

## Tasks
- [x] Task 1: Add `--key`/`--key-mode` parsing and key selection
  - Notes: extend `Options` with `std::string keyName` (empty = disabled)
    and `bool keyInverted`. `--key NAME` resolves via `XStringToKeysym` in
    main.cpp (X needed there; parsing validates only that a name was given);
    unknown keysym → error, usage, exit 1 (spec AC 3). `--key-mode
    normal|inverted` validated. Passing either flag enables watching.
    `--help` lists both (spec AC 7).
  - Touchpoints: `options.hpp`, `main.cpp` (`parseArgs` usage/validation,
    keysym resolution)
  - Done when: `--help` shows flags; bad key name rejected exit 1; defaults
    unchanged; tests updated for new option parsing paths.
- [x] Task 2: Track raw key state passively
  - Notes: in `CrosshairOverlay` ctor, when key watching is enabled, extend
    the XI2 mask with `XI_RawKeyPress|XI_RawKeyUp` (same `XISelectEvents`
    call pattern as raw motion; if XI2 unavailable, watching silently does
    nothing visible — document in code path). In the event drain, update a
    `keyHeld` member from `XI_RawKeyPress`/`XI_RawKeyUp` cookie events
    (compare `ev.xcookie.evtype`; extract keycode via
    `XIRawEvent::detail` and compare against the resolved keycode).
    Auto-repeat: XI2 raw events include repeat keydown/keyup pairs; state
    transitions dedupe naturally since keydown while held is a no-op.
  - Touchpoints: `main.cpp` (ctor XI2 setup, `waitForWakeup` event drain)
  - Done when: holding/releasing the key flips a logged state (temporary
    debug or observable behavior); no grabs anywhere in the diff (spec AC 4
    by review + manual typing test).
- [x] Task 3: Fold key state into visibility
  - Notes: generalize `evaluateIdle` to `evaluateVisibility(fadeMs,
    fadeDurationMs, idleMsSinceMotion, keyWantsVisible)` returning the same
    `IdleState` plus an `outKeyHidden` decision — final hide when either
    source says hide (spec: fade hides even while key held in normal mode;
    inverted suppression hides regardless of idle). In run(): compute
    `keyWantsVisible` from `keyHeld XOR keyInverted`; on transition, force
    a render bypassing the change guard (AC 1/2 "within one frame").
    Update tests for the new signature and combination matrix (spec AC 5).
  - Touchpoints: `options.hpp` (`evaluateVisibility`), `main.cpp` (`run()`
    driver), `tests/test_logic.cpp`
  - Done when: manual checks for AC 1, 2, 5, 6 pass; `ctest` green with
    new cases (spec AC 5, 7).
- [x] Task 4: Wake on key events and final integration
  - Notes: `waitForWakeup` already drains events; ensure the poll timeout
    in event mode wakes on key transitions (it does — key events arrive on
    the same connection) and that a key-down while idle-hidden re-shows
    only if key state says show. Confirm the no-argument path is
    byte-for-byte unchanged (watching disabled → no XI key mask, no state
    evaluation changes). Run the full flag matrix smoke test including
    interference typing test (spec AC 4, 6, 8).
  - Touchpoints: `main.cpp` (`run()`, `waitForWakeup`)
  - Done when: spec AC 1, 2, 4, 6, 8 verified manually.
- [x] Task 5: Docs and plan tick
  - Notes: README options table gains `--key` and `--key-mode` with the
    no-arg-unchanged note and the no-grab property; AGENTS.md documents the
    raw-key mechanism and the visibility-input combination. Run markdown
    table formatter.
  - Touchpoints: `README.md`, `AGENTS.md`
  - Done when: spec AC 9.

## Dependencies & prerequisites
- Tasks serial on the same files: 1 → 2 → 3 → 4 → 5.
- Prerequisite: X11 session for manual AC checks; `act`/container not
  affected (tests stay X-free).

## Verification
- See spec Acceptance Criteria and Verification Plan.
- Full flag smoke matrix after Task 4: default run unchanged; `--key`,
  `--key-mode`, combined with `--fade-ms`, `--poll`, `--no-selection`.

## Risks & mitigations
- Risk: some compositors/servers filter raw key events for privacy
  (rare) → Mitigate: spec Decisions names the fallback (passive modifier
  polling via XKB); if observed, implement and record in Task 2 notes
  rather than grabbing.
- Risk: keysym-to-keycode resolution is per-display (XStringToKeysym is
  display-independent, keycode mapping is not) → Mitigate: resolve keycode
  via `XKeysymToKeycode(display, keysym)` in the ctor after the display is
  open.
- Risk: raw key events change the event-mode wake semantics (previously
  only motion) → Mitigate: drain loop handles both; verify motion latency
  unchanged during Task 4 smoke run.

## Out of scope
- Toggle semantics, mouse triggers, multi-key, Wayland (spec Out of scope).

## Rollback
- See spec Rollback Plan (opt-in flags; revert restores today's behavior).
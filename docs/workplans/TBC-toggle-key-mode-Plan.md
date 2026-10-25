# TBC-toggle-key-mode: Toggle crosshair visibility on key press — Plan

## Spec

- Spec: `docs/workplans/TBC-toggle-key-mode-Spec.md`
- Jira: n/a

## Overview

- Outcome: Passing `--key-mode toggle` makes each press of the watched key
  flip crosshair/selection visibility; the overlay starts hidden. Hold
  semantics (`normal`/`inverted`), idle hiding, and no-grab raw-XI2 key
  watching are unchanged.
- Approach:
  - Add `keyToggle` to `Options` and accept `toggle` in `parseArgs`.
  - Extend the pure key-state machine in `options.hpp` with an edge-driven
    toggle latch so the combine logic stays X-free and unit-testable.
  - Wire the latch into `main.cpp`'s key handling and visibility loop;
    force a render on toggle transitions like the existing key changes do.

## Tasks

- [x] Task 1: Add `toggle` mode parsing and options state
  - Notes: Extend `--key-mode` usage text to `normal|inverted|toggle`;
    store the mode on `Options` instead of only the `keyInverted` bool
    (add `keyToggle = false`), keeping `keyWatching` semantics unchanged.
  - Touchpoints: `options.hpp` (`Options`, `parseArgs`, `printUsage`)
  - Done when: `--key-mode toggle` parses without error, invalid modes are
    still rejected with usage/exit 1 (spec AC 1, 7), and `--help` lists
    all three modes (spec AC 10).

- [x] Task 2: Extend the pure visibility/key state machine for toggle
  - Notes: Add toggle-latch handling to the state machine alongside
    `evaluateVisibility`: a press edge flips the latch; holding/repeat does
    not; release is a no-op. Combine with idle hiding using the existing
    hide-if-either rule. Keep the new logic X-free so it can be exercised
    directly from the unit-test harness.
  - Touchpoints: `options.hpp` (state machine, `evaluateVisibility`)
  - Done when: press/release/repeat/combine-with-idle sequences behave per
    spec AC 1–3 and 5 at the state-machine level.

- [x] Task 3: Wire the toggle latch into the main event/poll loop
  - Notes: In the XI2 raw key event path, feed `XI_RawKeyPress` (and ignore
    repeats/release for the latch) into the toggle latch. Treat a toggle
    transition as a key change so it bypasses the change guard and renders
    immediately. Ensure the `--poll` path handles the latch correctly and
    that the overlay starts hidden when toggle mode is active.
  - Touchpoints: `main.cpp` (XI2 event handling, render guard, poll path)
  - Done when: manual X11 checks match spec AC 1, 2, 4, 6, 8: starts
    hidden, each press flips visibility, other apps still receive the key,
    and `--poll` behaves the same as event mode.

- [x] Task 4: Add unit tests for parsing and toggle state
  - Notes: Cover `--key-mode toggle` parsing, invalid-mode rejection, and
    latch sequences (press→visible, release→no change, repeat→no advance,
    combine with idle hiding). Extend the existing assert-harness patterns
    in `tests/test_logic.cpp`.
  - Touchpoints: `tests/test_logic.cpp`
  - Done when: `ctest` passes with the new tests and existing tests still
    pass (spec AC 9).

- [x] Task 5: Document the new mode
  - Notes: Update the runtime-options table in README.md and the key-
    watching notes in AGENTS.md to describe `--key-mode toggle`, its
    start-hidden default, and that it uses the same no-grab raw XI2
    mechanism.
  - Touchpoints: `README.md`, `AGENTS.md`
  - Done when: docs mention the `toggle` mode (spec AC 10) and CI/lint
    checks are unaffected by docs changes.

## Dependencies & prerequisites

- Task 2 depends on Task 1 (needs the `keyToggle` option).
- Task 3 depends on Task 2 (main loop feeds the state machine).
- Task 4 depends on Tasks 1 and 2 (tests target parsing and the latch).
- Task 5 can run anytime after Task 3 so documented behavior matches the
  implementation.

## Verification

- See spec Acceptance Criteria and Verification Plan for manual X11 checks
  (transitions, repeat, no-grab interference, fade interplay, poll parity).
- Extra build-time check: run `make && ctest` after each code task to keep
  the CI parity locally; compile with the project's warning flags to keep
  `-Werror` clean.

## Risks & mitigations

- Risk: Auto-repeat could advance the toggle latch multiple times per
  physical press → Mitigate: filter repeats in the state machine by only
  flipping on a down-edge (press while already down is ignored); verify
  manually per spec AC 3.
- Risk: Poll mode bypasses the XI2 event path, so the latch might not
  update → Mitigate: route both event-driven and poll updates through the
  same state-machine update; add a poll-mode unit check in Task 4.

## Out of scope

- Changes to `normal`/`inverted` hold semantics; mouse-button or multi-key
  toggles; persisting toggle state across restarts; mixing toggle with
  hold modes (per spec Out of scope).

## Rollback

- See spec Rollback Plan: revert the commit series; `toggle` is opt-in and
  the default/no-argument behavior is unchanged.

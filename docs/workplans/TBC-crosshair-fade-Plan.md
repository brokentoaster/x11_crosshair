# TBC-crosshair-fade: Hide overlay when pointer is idle — Plan

## Spec
- Spec: docs/workplans/TBC-crosshair-fade-Spec.md
- Jira: n/a

## Overview
- Outcome: the overlay hides itself after the pointer has been still for
  `--fade-ms` milliseconds and reappears instantly on motion; optional soft
  fade-out if plain-Xlib interpolation looks right.
- Approach: the run loop already detects motion (XI2 events or polling) and
  re-applies `ShapeBounding` every frame. Add an idle state machine on top:
  track last-motion time, and when the timeout elapses apply an empty
  bounding region instead of the crosshair rects. Skip drawing work while
  hidden and lengthen the event-mode poll wait slightly (still bounded so
  selection updates keep rendering when relevant — actually not needed while
  hidden: text box hides too).

## Tasks
- [x] Task 1: Add `--fade-ms` flag with idle-hide state machine
  - Notes: extend `Options` with `fadeMs` (0 = disabled, the default so
    no-arg behavior is unchanged). In `run()`, on each loop: if pointer moved,
    reset `last_motion` and render normally. If `fadeMs > 0` and
    `now - last_motion >= fadeMs`, set an empty `ShapeBounding` region and
    skip drawing/raising until the next motion. Keep the window mapped and
    the `ShapeInput` region untouched (spec Decisions). While hidden, event
    mode can `poll` a longer wait (e.g. 250 ms) since any XI2 event wakes it
    immediately anyway.
  - Touchpoints: `main.cpp` (`Options`, `parseArgs`, `run()`)
  - Done when: spec AC 1–8 verified manually (move/hold still test, default
    behavior unchanged, poll mode, click-through while hidden, CPU check).
- [x] Task 2: Try soft fade-out; keep or cut by feasibility
  - Notes: candidate approach — after the idle timeout elapses, run a short
    sequence of renders interpolating the crosshair color toward
    transparency. With a 32-bit TrueColor visual the fill GC color alpha can
    step down (fewer intensity steps, ~10 frames over the duration); with the
    stipple GC, reduce visible length or rely on alpha steps. Guard: if a
    clean implementation exceeds ~40 lines or needs a second visual/compositing
    dependency, cut it and record "not implemented" (spec AC 9 allows this).
    Wire as `--fade-duration MS` (fade duration after the timeout; only
    meaningful with `--fade-ms > 0`).
  - Touchpoints: `main.cpp` (render section of `run()`)
  - Done when: implemented → fade visually ramps out over the duration and
    `--help` documents it (AC 9); cut → `--help` lists only `--fade-ms`, and
    the cut is noted in the commit message and README table.
- [x] Task 3: Update README.md and AGENTS.md, tick plan
  - Notes: README options table gains `--fade-ms` (and the fade flag if kept)
    with the chosen defaults; AGENTS.md notes the idle-hide state machine and
    the shape-only hiding invariant. Run the markdown table formatter.
  - Touchpoints: `README.md`, `AGENTS.md`
  - Done when: spec AC 10; docs contain no contradictions with the flags
    actually shipped.

## Dependencies & prerequisites
- Task 2 after Task 1 (fade builds on the idle state machine).
- Task 3 last (docs must match what shipped).

## Verification
- See spec Acceptance Criteria and Verification Plan.
- Per-task manual checks as in "Done when"; no test suite exists.

## Risks & mitigations
- Risk: alpha-stepped color fill may not fade against all backgrounds (the
  stippled black contrast lines have no alpha path) → Mitigate: fade both
  GC colors together; if visually poor, cut per Task 2 guard rather than
  shipping a half-effect.
- Risk: hidden state could fight the redraw-on-change guard (nothing changes
  → loop may never re-evaluate the timeout) → Mitigate: evaluate idle
  transition before the change guard, and give the hidden loop an explicit
  wake deadline (poll timeout = remaining idle time).

## Out of scope
- Fade-in, keyboard/window-based hiding, per-window config (spec Out of scope).

## Rollback
- See spec Rollback Plan (revert the commit series).
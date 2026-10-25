# TBC-toggle-key-mode: Toggle crosshair visibility on key press — Spec

## Goal

The user can press a keyboard key to switch the crosshair on or off: each
press (down-and-up) of the watched key flips visibility. The overlay starts
hidden when this mode is selected. Key listening stays passive (raw XI2
events, no grabs) and combines with the existing hold-based key modes and
idle hiding without breaking either.

## Context

Key watching already exists (`--key`/`--key-mode normal|inverted`) using
XInput2 raw key events on the root window, with hold-only semantics; the
original spec (TBC-key-toggle) explicitly deferred toggles. Toggle semantics
differ fundamentally: they need edge detection (press = transition, not
level) and persistent state (a latch), so they cannot be expressed by the
current per-frame "is the key down?" query.

## Scope

### In scope

- New `--key-mode toggle` value: each press of the watched key toggles
  crosshair visibility on/off; the overlay starts hidden in this mode.
- Toggle state is edge-driven: a press event (key down) flips the latch;
  release and auto-repeat do not flip it again.
- Combines with idle hiding: visible only when the toggle latch says show
  AND the pointer is not idle (with `--fade-ms`). A press while idle-hidden
  flips the latch; visibility returns only on the next motion.
- The selection text box follows the same visibility as the crosshair.
- Toggle state resets to hidden if the watched key is changed at runtime
  (not possible today — noted only if a future flag reload adds it).
- `--help` and README/AGENTS.md document the new mode value.
- Unit tests for the toggle latch logic in the X-free state machine.

### Out of scope

- Changes to `normal`/`inverted` hold semantics.
- Toggling via mouse buttons or multiple keys.
- Persisting the toggle state across restarts (always starts hidden).
- Toggle plus hold-mode simultaneously (single mode value, no mixing).

## Constraints & Assumptions

- X11 invariants from AGENTS.md preserved: shape-only hiding, window stays
  mapped, `ShapeInput` untouched, `XFlush` placement.
- Passive XI2 raw key events only — no `XGrabKey`/`XGrabKeyboard`; watched
  key keeps reaching other applications.
- Latency: a press must be reflected within ~1 frame (<50 ms).
- Must work in both event mode and `--poll` mode (poll mode skips XI2
  setup; in poll mode the key watch uses the same periodic raw-event check
  the hold modes use — must not regress the no-XI2 polling path).
- Auto-repeat produces repeat press events; the latch must ignore repeats.

## Decisions

- Mechanism: reuse the existing `XI_RawKeyPress`/`XI_RawKeyRelease`
  selection on the root window; a raw press event flips the toggle latch.
  Repeats are filtered by tracking key-down state (only flip on a
  down-edge: press while already down is ignored).
- The toggle latch lives with the key state machine and feeds
  `evaluateVisibility` (options.hpp) so the pure combine logic stays
  X-free and unit-testable; the main loop only feeds it raw events.
- `toggle` is a third allowed value of `--key-mode`; passing it enables
  key watching exactly like `normal`/`inverted` do.
- Visibility combines as hide-if-either-source-says-hide, matching the
  existing hold-mode rule, so idle hiding and the key source both gate.

## Acceptance Criteria

1. `./x11_crosshair --key-mode toggle` starts with the crosshair and
   selection box hidden; each press of the watched key (default `Shift_L`)
   switches visibility on or off, within one frame of the transition.
2. Releasing the key after a press does not flip visibility again; the
   next distinct press does.
3. Auto-repeat (holding the key down with repeat enabled) does not cause
   the visibility to flicker or advance the latch more than once per
   physical press.
4. The watched key still reaches other applications (typing in a focused
   terminal works with the overlay running; no grab behavior).
5. Combined with `--fade-ms`: crosshair is visible only when the toggle
   latch says show AND the pointer moved recently; pressing the key while
   idle-hidden flips the latch but does not force drawing until motion.
6. `--key Control_L --key-mode toggle` watches left Control with the same
   toggle behavior; an unknown keysym name is rejected with usage, exit 1.
7. `--key-mode bogus` is rejected with usage and exit 1, listing the valid
   modes.
8. Works in `--poll` mode identically to event mode.
9. The toggle latch logic is covered by unit tests in `tests/test_logic.cpp`
   (press/release/repeat/combine-with-idle sequences).
10. `--help`, README.md, and AGENTS.md document the `toggle` mode value.

## Verification Plan

- Manual X11 session for AC 1–8: run each combination, press the key,
  confirm on/off transitions with a screen recording; hold the key for
  repeat behavior (AC 3); type in a terminal while toggling (AC 4).
- `ctest` covers AC 9 via the new unit tests.
- Flag validation (AC 6, 7) via exit codes and usage output.
- Code review against AGENTS.md invariants (shape-only hiding, no grabs).

## Rollback Plan

- Revert the commit series. `toggle` is an opt-in mode value; the default
  `normal` behavior and no-argument startup are unchanged, so reverting
  restores prior behavior fully.

## Open Questions

- None currently.

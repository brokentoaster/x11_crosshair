# TBC-key-toggle: Show/hide crosshair on key press — Spec

## Goal

The user can hold a keyboard key to control crosshair visibility: in normal
mode the crosshair is visible only while the key is held; in inverted mode
the crosshair is always visible and suppressed while the key is held. The
key and the mode are selectable at runtime. Key listening must not interfere
with any other X11 operation — no grabs, no key events consumed by other
clients, and no change to how the overlay's existing click-through behavior
works.

## Context

The overlay currently controls visibility only by pointer idle
(`--fade-ms`). Some users want on-demand visibility tied to a key (e.g. hold
Shift to see the crosshair only while aiming; or hold a key to temporarily
hide it while presenting). The overlay window is click-through via an empty
`ShapeInput` region, so it receives no input events; visibility must
therefore come from passive monitoring of the keyboard state, not from the
overlay window.

## Scope

### In scope

- New flags (both opt-in: passing either one enables key watching; with
  neither, behavior is unchanged — always visible):
  - `--key <keysym-or-name>` — selects the watched key; default `Shift_L`
    (left Shift) once watching is enabled.
  - `--key-mode <normal|inverted>` — default `normal` (visible only while
    held). `inverted` = visible by default, suppressed while held.
  - Both flags must be listed in `--help` with defaults.
- Visibility rule: while the watched key is held, `normal` shows the
  crosshair and `inverted` hides it; when not held, the reverse. The rule
  combines with existing features:
  - Without `--fade-ms`, key state alone determines visibility.
  - With `--fade-ms`, the overlay is visible only when the key state says
    show AND the pointer is not idle (both conditions must agree to draw).
  - The selection text box follows the same visibility as the crosshair.
- Passive key monitoring via XKB/XInput2 that does not grab the key (see
  Decisions). If true grab-free monitoring proves impossible for some
  keys, the limitation is documented rather than silently grabbing.
- Interaction with pointer-idle hiding: while suppressed (either reason),
  the render loop skips drawing exactly as the idle-hidden path does today.

### Out of scope

- Toggles (press to show, press again to hide) — hold-only semantics.
- Mouse-button triggers.
- Watching multiple keys or modifier combinations beyond a single key.
- Wayland/XWayland key handling.
- Auto-repeat handling beyond what the chosen mechanism requires.

## Constraints & Assumptions

- X11 invariants from AGENTS.md preserved: shape-only hiding, window stays
  mapped, `ShapeInput` untouched, `XFlush` placement.
- **No active grabs**: `XGrabKeyboard`/`XGrabKey` would steal keys from
  other applications and violate the "must not interfere" requirement. The
  implementation must observe key state passively.
- The overlay window is click-through and receives no events; key state
  must come from a mechanism that works without window input focus.
- No new external dependencies beyond what X11/XFIXES/XI/XKB already
  provide in the image and the repo's dependency convention.
- Latency: key state changes must be reflected within ~1 frame (<50 ms).
- Must work in both event mode and `--poll` mode.

## Decisions

- Mechanism: XInput2 raw key events (`XI_RawKeyPress`/`XI_RawKeyUp`) with
  `XI_RawKeyMask` selected on the root window. Raw events are delivered to
  all clients that select for them regardless of focus or grabs, and
  selecting them does not consume keys — other clients receive their own
  events normally. This matches the existing XI2 raw-motion pattern.
  - Fallback if raw key events do not arrive for the chosen key (some
    servers/compositors filter certain keys): passive query of the modifier
    state via `XkbGetIndicatorState`-style polling is the documented
    fallback; decide during implementation based on observed behavior and
    record the outcome in the plan.
- Default key `Shift_L`; the `--key` value accepts keysym names
  (`Shift_L`, `Control_L`, `Alt_L`, `Caps_Lock`, ...), resolved via
  `XStringToKeysym`; unknown names are rejected with usage and exit 1.
- Key-down/key-up transitions are handled as state, not events, so missed
  events self-correct on the next transition.

## Acceptance Criteria

1. `./x11_crosshair --key-mode normal` (no other flags) hides the
   crosshair and selection box; holding left Shift shows them; releasing
   hides them again within one frame of the transition.
2. `./x11_crosshair --key-mode inverted` shows the crosshair normally;
   holding left Shift hides it; releasing restores it within one frame.
3. `./x11_crosshair --key Control_L` watches left Control instead: in
   normal mode the crosshair shows only while Control is held. A keysym
   name that does not exist (e.g. `--key Not_A_Key`) is rejected with an
   error, usage, and exit 1.
4. Holding or releasing the watched key does not interfere with other
   applications: the key still reaches a focused terminal (typing works,
   shortcuts fire), and no other client's key behavior changes (no grab —
   verified by typing in a terminal while holding Shift repeatedly).
5. Combined with `--fade-ms 1000`: crosshair is visible only when the key
   is held (normal mode) AND the pointer moved recently; idle hides it even
   while the key is held; motion re-shows it only if the key state says
   show.
6. Works in `--poll` mode identically to event mode.
7. `--help` documents `--key` (default `Shift_L`) and `--key-mode`
   (default `normal`).
8. Auto-repeat does not toggle visibility spuriously (repeat produces no
   visible flicker).
9. README.md and AGENTS.md document the new flags and the no-grab property.

## Verification Plan

- Manual X11 session for AC 1–6: run each combination, hold/release the
  key, and use a stopwatch/screen recording to confirm ~1-frame transitions.
- Interference check (AC 4): focus a terminal, hold and release Shift and
  the selected key repeatedly while typing; confirm characters and
  shortcuts behave identically with the overlay running and not running.
- Repeat flicker check (AC 8): hold a character key (auto-repeat) mapped
  via `--key` and confirm no flicker.
- Flag validation (AC 3): `--key Not_A_Key` rejected with exit 1.
- Code review against AGENTS.md invariants (shape-only hiding, no grabs).

## Rollback Plan

- Revert the commit series. Key watching is opt-in (enabled only when
  `--key` or `--key-mode` is passed), so the no-argument default keeps
  today's always-visible behavior and no existing user's behavior changes.
  Reverting restores it fully.

## Open Questions

- Whether `inverted` should also suppress the idle-hide feature (proposal:
  no; they combine as defined in Scope).
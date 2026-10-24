# TBC-crosshair-fade: Hide overlay when pointer is idle — Spec

## Goal

The overlay disappears when the cursor has been still for a user-chosen
number of milliseconds, and reappears as soon as the pointer moves. This
removes the visual distraction of a permanent crosshair when the user is not
actively moving the pointer (e.g. reading, watching a video), while keeping
the crosshair instantly available during motion.

## Context

Today the overlay always draws the crosshair (and selection text box) for as
long as the program runs, regardless of pointer activity. The event loop
already knows when the pointer moves (XI2 raw-motion events or polling
round-trips in `--poll` mode), so an idle timeout can be derived from
information the loop already has.

## Scope

### In scope

- Idle-timeout behavior: when the pointer has not moved for N ms, the overlay
  stops rendering (window shape cleared so nothing is visible); on the next
  pointer motion the crosshair reappears immediately. N defaults to a
  sensible constant chosen during implementation and is settable via a flag
  (e.g. `--fade-ms N`; N = 0 means never hide, preserving current behavior).
- A soft fade-out option (e.g. `--fade-steps N` or `--fade-duration MS`) if
  simple to implement: the overlay gradually fades over the configured
  duration instead of switching off abruptly. If the implementation turns out
  to be disproportionate (see Constraints), this may be cut; the spec treats
  it as optional.
- Whether the selection text box is also hidden while idle (yes — it hides
  together with the crosshair, since it follows the pointer).
- Interaction with existing flags must be defined: idle hiding applies in
  both event mode and `--poll` mode, and with any `--thickness` value.

### Out of scope

- Fading in on wake (the crosshair reappears at full opacity instantly).
- Hiding the overlay while the pointer is over certain windows, or hiding
  based on keyboard activity.
- Changes to selection fetching, sanitization, or shape mechanics beyond the
  idle-on/off switch.
- Any per-window or per-monitor configuration.

## Constraints & Assumptions

- X11 invariants from AGENTS.md must be preserved: visibility is controlled
  via `ShapeBounding` (an empty region hides the overlay while keeping the
  window alive); the window must remain mapped so re-showing is cheap and
  input stays click-through via the empty `ShapeInput` region.
- Soft fade must be implementable with plain Xlib calls only (e.g. interpolating
  the crosshair line colors or shrinking the bounding region); no compositing
  extensions or new libraries. If color interpolation cannot look right with
  the 32-bit visual / stipple GC approach in a small patch, drop the fade-out
  option and keep the abrupt off-switch — decided during implementation and
  recorded in the plan.
- While hidden, CPU usage must not increase (the loop may sleep longer when
  idle, but must wake promptly on pointer motion in event mode).
- The program never hides the overlay while the pointer is moving; the
  timeout is strictly measured from the last observed motion.
- Keep the single-file, no-new-dependency convention.

## Decisions

- Hide via clearing `ShapeBounding` (empty region), not unmapping the window,
  so re-show is a cheap shape update and input click-through state is
  untouched.
- `--fade-ms 0` (or a suitable explicit off value) disables idle hiding;
  the default value is chosen at implementation time and documented in the
  README options table.
- The selection text box is hidden together with the crosshair.

## Acceptance Criteria

1. `./x11_crosshair --fade-ms 1000` shows the crosshair while the pointer
   moves, and the overlay becomes invisible after the pointer has been still
   for 1 second.
2. Moving the pointer after the overlay has hidden makes the crosshair
   reappear immediately (well under 100 ms from motion to visible).
3. `./x11_crosshair` with no `--fade-ms` argument behaves exactly as today
   (never hides), preserving existing users' behavior.
4. `--fade-ms 0` explicitly disables idle hiding.
5. Idle hiding works in both event mode and `--poll` mode, including
   `--poll 50`.
6. While hidden, the selection text box is also not visible; it reappears
   with the crosshair on motion (if new selection text arrives, it shows on
   next motion or is simply not displayed until then).
7. While the overlay is hidden, CPU usage does not increase compared to the
   visible idle state (check with `top`/`pidstat`).
8. The window remains click-through at all times, including while hidden and
   during the fade.
9. If the soft fade option is implemented: `--fade-duration MS` (or the
   chosen flag) makes the overlay fade out over approximately MS milliseconds
   after the idle timeout elapses, and `--help` documents it. If cut, AC 9 is
   recorded as not implemented and `--help` must not list it.
10. `--help` lists the new flags with defaults; README.md and AGENTS.md
    document them.

## Verification Plan

- Manual X11 session: run with `--fade-ms 1000`, move then hold the pointer
  still, confirm the overlay vanishes after ~1 s and reappears instantly on
  motion (stopwatch or screen recording for AC 1–2).
- Run default invocation and `--fade-ms 0` to confirm AC 3–4.
- Repeat the idle test under `--poll 50` for AC 5.
- Copy text while idle to check AC 6.
- Observe `top` while idle-hidden for AC 7.
- Click through the hidden overlay into a terminal for AC 8.
- Code review against AGENTS.md X11 invariants (shape-only hiding, no unmapping).

## Rollback Plan

- Revert the commit series; idle hiding is additive and behind flags, so
  reverting restores the always-visible behavior. No data or config affected.

## Open Questions

- Exact flag names (`--fade-ms` for the timeout, `--fade-duration` vs
  `--fade-steps` for the soft fade) — settle at plan/implementation time.
- Default timeout value if any (proposal: disabled by default, i.e. behave
  as today, to avoid surprising existing users).
- Whether the soft fade survives feasibility review; the cut condition is
  spelled out in Constraints.
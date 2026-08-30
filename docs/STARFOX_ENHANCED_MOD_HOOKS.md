# Star Fox Enhanced mod hook map

This hook map translates the useful Enhanced behavior into StarFoxSNESRecomp
terms. It is intentionally narrow: every hook below names the symbol evidence,
the Enhanced behavior, and the safest recomp-side implementation shape.

## Crosshair color

Enhanced applies crosshair color as a host-side palette transform. Its
`apply_crosshair_tint` routine tints OBJ palette row 4, starting at palette
index `128 + 4 * 16`, preserving brightness while replacing hue. It also uses
`M_HUDCOLOUR` for the companion Super FX cockpit triangles.

Reviewed symbols:

| Symbol | Address | Purpose |
|--------|---------|---------|
| `CROSSHAIRON` | `$00:1633` | Crosshair visibility flag. |
| `ARSEBANDX` | `$00:1777` | Crosshair X state. |
| `ARSEBANDY` | `$00:1779` | Crosshair Y state. |
| `DO_CROSSHAIR` | `$03:D4CE` | Candidate draw routine. |
| `M_HUDCOLOUR` | `$70:3512` | Super FX HUD/cockpit color source. |

Recomp path:

Status: implemented as `CrosshairColor` in `config.ini`.

Implementation notes:

1. The default `Original` setting leaves retail rendering unchanged.
2. Non-original settings save OBJ palette row 4, tint entries
   `128 + 4 * 16 + 1..15` for one frame render, then restore emulated CGRAM
   before the next simulation step or savestate.
3. The frame pre-hook temporarily points Super FX RAM `M_HUDCOLOUR` at
   `128 + 4 * 16 + 15` so the original MHUD/cockpit triangle pass uses the
   same bright tinted entry. The post-hook restores the original byte before
   save-state or subsequent gameplay state can observe the host override.

This is the lowest-risk first mod because it can avoid game-state mutation.

## God mode

Enhanced applies god mode at gameplay/training strategy boundaries. It
reasserts a no-collision bit and keeps the player's Nova Bomb count at a floor
of three while preserving higher pickup counts.

Reviewed symbols:

| Symbol | Address | Purpose |
|--------|---------|---------|
| `PSHIPFLAGS3` | `$00:1563` | Bit `0x08` is reasserted for no-collision. |
| `SPECWEPCNT` | `$00:1634` | Nova Bomb count floor. |
| `SPECIALDELAY` | `$00:15B1` | Post-fire delay; used by God Nuke. |
| `PCBOXOBJ_B` | `$00:15F2` | Player body collision-object pointer. |
| `PCBOXOBJ_LW` | `$00:15EE` | Player left-wing collision-object pointer. |
| `PCBOXOBJ_RW` | `$00:15F0` | Player right-wing collision-object pointer. |

Recomp path:

Status: implemented as `GodMode` in `config.ini`.

Implementation notes:

1. The default `0` setting leaves retail gameplay unchanged.
2. While active gameplay objects are present, the host reasserts
   `PSHIPFLAGS3 |= 0x08` before and after each frame's simulation call.
3. `SPECWEPCNT` is floored at three with a word write, preserving higher counts
   gained from pickups.
4. The shared recomp-ui Mods view exposes this setting through the built-in
   Star Fox Enhanced mod provider. It still needs route and transition audits
   before it should be treated as parity with the source port.

This is feasible but higher-risk than crosshair color because it mutates game
state.

## God Nuke

Enhanced does not replace the whole weapon system. It lets the normal nuke be
created, marks the newly fired nuke when R+A is pressed, then detonates by
walking active objects after the nuke enters its explosion/null state. It avoids
softlocks by protecting scripted boss shapes.

Reviewed symbols:

| Symbol | Address | Purpose |
|--------|---------|---------|
| `FIRE_NUKE` | `$21:A784` | Candidate normal nuke fire routine. |
| `NUKE` | `$00:97A0`, `$11:EDB4` | Shape/header aliases. |
| `NUKE_ISTRAT` | `$21:A241` | Nuke initializer strategy. |
| `NUKE_STRAT` | `$21:A2B4` | Nuke active strategy. |
| `NUKEEXP_STRAT` | `$21:A373` | Nuke explosion strategy. |
| `NUKEEXP_ISTRAT` | `$21:A332` | Nuke explosion initializer. |

Recomp path:

Status: implemented as `GodNuke` in `config.ini`, gated by `GodMode`.

Implementation notes:

1. The frame hook snapshots active `NUKE` object slots before simulation when A
   is newly pressed.
2. After simulation it finds newly created nuke slots, shortens
   `SPECIALDELAY` to four, and arms the slot only when R was held with A.
3. Armed slots detonate when the same active object slot becomes `NULLSHAPE` or
   reaches `NUKEEXP_STRAT`.
4. Detonation skips player collision objects, friend-collision objects,
   negative-health objects, active nuke/nuke-explosion objects, and Andross
   shapes. Macbeth boss components receive normal nuke damage instead of an
   autokill, matching the Enhanced softlock guard.

This is still higher-risk than crosshair color because it mutates active object
state and depends on route/boss-specific scripts.

## Widescreen graduation

Enhanced reinforces the renderer split: keep gameplay simulation at the
original tick rate and expand presentation/projection only. Star Fox no longer
uses a PPU/Super FX replay widescreen path; widescreen output is owned by the
opt-in native renderer.

Reviewed symbols:

| Symbol | Address | Purpose |
|--------|---------|---------|
| `TRANSFER_L` | `$02:D53B` | 20 Hz transfer/update boundary in Enhanced docs. |
| `VIEWPOSX` | `$00:00B4` | Camera X. |
| `VIEWPOSY` | `$00:00B6` | Camera Y. |
| `VIEWPOSZ` | `$00:00B8` | Camera Z. |
| `M_VANISHX` | `$70:0034` | Projection center X. |
| `M_VANISHY` | `$70:0036` | Projection center Y. |

Recomp path:

1. The shared recomp-ui Mods view exposes Enhanced-style Display Mode presets:
   `16:10`, `16:9`, `21:9`, and `32:9`. Selecting a wide mode enables the
   native renderer because Star Fox stock rendering is always 4:3.
2. `DisplayMode` config aliases now accept Enhanced's display modes:
   `0`/`4:3`, `1`/`16:9`, `2`/`16:10`, `3`/`21:9`, and `4`/`32:9`.
   The shared renderer/Super FX caps were raised so those modes are represented
   directly rather than clamped.
3. The old `WidescreenHud*` controls are parse-only compatibility keys. They
   are not shown in the Mods view and do not affect Star Fox rendering.
4. Use scripted captures for training, Corneria, each route branch, boss scenes,
   comms/portraits, and death/continue transitions.
5. Native renderer follow-up work should port the PC renderer's background,
   sprite/HUD, text, and particle layers rather than reintroduce PPU margins.

## Presentation frame rates

Enhanced supports 20, 30, 60, 90, 120, 240, 360, and 480 FPS presentation while
preserving deterministic gameplay updates. That transfer does not map directly
to the current recomp loop: `RtlRunFrame` advances one SNES vblank per host
iteration, and both SDL/OpenGL presentation paths are vsynced today.

Recomp path:

Status: partially implemented as `PresentationFPS` in `[General]`.

Implementation notes:

1. `20`, `30`, and `60` are implemented as host render cadence modes. They do
   not skip `RtlRunFrame`, so SNES simulation timing remains unchanged.
2. Lower cadence modes skip SDL/OpenGL presents on non-presented frames while
   still running the title `draw_ppu_frame` path. That preserves HDMA and
   raster events.
3. `90`, `120`, `240`, `360`, and `480` are accepted and use duplicate-present
   scheduling from the newest retained final frame. SDL/OpenGL vsync is disabled
   for these modes so extra presents are not pinned to a 60 Hz swap interval.
4. High-FPS modes still do not implement Enhanced's transform interpolation;
   repeated presentations are real, but object/camera motion is not smoothed
   between simulated SNES frames yet.

## Framework candidate

Enhanced's useful shared framework lesson is observability and opt-in extension
points, not generic renderer code. The current snesrecomp tree already has
frame-model notes, audio counters, frame fingerprints, debug-server history,
and StarFoxSNESRecomp now has a bounded `--frames N` run mode for repeatable
local captures. The first renderer-facing extension is
`RtlGameInfo.enhanced_render_frame`, which lets a title replace or supplement a
host presentation buffer only when the game explicitly enables it. Star Fox's
current implementation owns widescreen output, not a PPU margin supplement. The
native bridge now builds directly against Star Fox Enhanced's
`BackgroundRenderer` and `SpriteRenderer` for BG/OAM output. The previous local
C Super FX overlay is disabled by default because it is not faithful enough for
normal output; full `SoftwareRenderer` parity, text, particles, cockpit HUD, and
presentation effects still remain.

The concrete follow-up is a small title-neutral presentation diagnostics facade
that reports:

- simulated frame count;
- rendered/presented frame count;
- guest-frame pacing state;
- optional title counters mapped from reviewed symbols.

That should be tracked as snesrecomp framework work after Star Fox proves the
title-side hook map in practice.

## Live FPS readout

Enhanced persists an on-screen FPS counter setting. StarFoxSNESRecomp already
had the renderer FPS/perf overlay and title-bar measurement path; the portable
piece is making the on-screen overlay part of config state and sampling actual
presentations instead of render cost.

Recomp path:

Status: implemented as `ShowFPS` in `[General]`.

Implementation notes:

1. `ShowFPS = 1` initializes the existing `g_display_perf` overlay at startup.
2. The existing `F` hotkey remains a session-local toggle.
3. The counter samples completed host presentations every 250 ms, matching the
   reference behavior more closely than the previous draw-cost reciprocal.
4. This still does not implement Enhanced's high-FPS transform interpolation.

## Presentation debugger

Enhanced exposes a presentation-history debugger with freeze, step-forward, and
step-back controls. The recomp can port the retained-frame behavior directly
because it already has a single completed host presentation buffer per
displayed SNES frame.

Recomp path:

Status: implemented with rebindable `PresentationDebug`,
`PresentationStepForward`, and `PresentationStepBack` key actions.

Implementation notes:

1. The host stores the last 120 completed final presentation buffers.
2. `PresentationDebug` freezes presentation on the newest retained frame
   without running another SNES frame.
3. `PresentationStepBack` walks backward through retained frames while frozen.
4. `PresentationStepForward` walks toward the newest retained frame; at the
   live edge it advances exactly one simulated frame and then remains frozen.
5. Defaults are `Ctrl+F5`, `Ctrl+F6`, and `Ctrl+F7` because plain `F5` through
   `F7` are already load-state bindings in this recomp.

## Mouse, Super Scope, and free camera

Enhanced's mouse and Super Scope support is primarily Star Fox EX support. It
writes EX-only symbols such as `MOUSEMODE`, `MOUSE_X1`, `MOUSE_Y1`,
`SCOPEMODE`, `SCOPE_H`, and `SCOPE_V`; those symbols are not present in the
retail Star Fox recomp target.

Reviewed retail-adjacent symbols:

| Symbol | Address | Purpose |
|--------|---------|---------|
| `ARSEBANDX` | `$00:1777` | Crosshair X displacement consumed by `DO_CROSSHAIR`. |
| `ARSEBANDY` | `$00:1779` | Crosshair Y displacement consumed by `DO_CROSSHAIR`. |
| `CROSSHAIRON` | `$00:1633` | Crosshair visibility flag. |

Recomp path:

Status: not implemented.

Implementation notes:

1. A direct post-frame write to `ARSEBANDX/Y` would not own the current frame's
   reticle because `DO_CROSSHAIR` consumes those values while the game builds
   sprite/OAM state during simulation.
2. A pre-frame write is also unstable because the retail aim/camera logic
   recalculates the values before `DO_CROSSHAIR`.
3. A faithful port needs either EX ROM/source ingestion or a validated
   replacement for the retail crosshair OAM generation boundary.
4. Enhanced's free camera is a native-renderer presentation feature based on
   interpolated camera transforms. The current recomp path renders the original
   Super FX framebuffer plus widescreen replay, so there is no equivalent
   camera hook yet without expanding the Super FX presentation renderer.

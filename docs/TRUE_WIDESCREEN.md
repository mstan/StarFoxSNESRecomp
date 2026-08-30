# Star Fox native widescreen rendering

## Attribution

Star Fox native widescreen is adapted from `DisplayMode` and renderer work in
kandowontu's Star Fox Enhanced decomp:
<https://github.com/kandowontu/starfox-enhanced>. Star Fox Enhanced is the
authoritative full-experience project; StarFoxSNESRecomp's contribution is the
development reference integration layer that maps retail Star Fox runtime state
into that separate native renderer model. Credit for the widescreen renderer
design, reference implementation, and presentation frame-rate behavior belongs
to the Star Fox Enhanced author and project.

Star Fox widescreen is no longer a Star Fox-specific modification of the stock
SNES renderer. The stock path renders the authentic 256x224 picture. Wider
Star Fox output belongs exclusively to the opt-in native renderer path behind
`EnhancedRenderer`.

## Contract

- Stock renderer: 256x224, no Star Fox Super FX replay widening, no Mode 2
  side capture, no HUD/OAM anchoring, no side-margin post-processing.
- Native renderer: owns the whole presentation framebuffer when enabled.
  `DisplayMode`/`Widescreen` only changes the effective output width when
  `EnhancedRenderer = 1`.
- Compatibility: old `WidescreenHud*` config keys may still parse, but they are
  not written by the default config path and are not consumed by Star Fox RTL.

## Current Native Path

`StarFoxEnhancedRenderFrame` handles the frame before the default presenter runs.
It clears the full target, lets `StarFoxDrawPpuFrame` advance the game's normal
PPU/HDMA render state, renders BG1/BG2/BG3 and OAM through Star Fox Enhanced's
`BackgroundRenderer` and `SpriteRenderer` into an indexed framebuffer from raw
PPU VRAM/CGRAM/OAM/register state, and converts that framebuffer to the host
BGRA target.
Mode 2 offset-per-tile state is enabled from the live PPU mode rather than the
transient retail `DOVOFS` calculation request. By post-frame, `DOVOFS` can be
clear even though BG3 VRAM still contains the active validity-tagged offsets;
following the PPU mode preserves Corneria's mountains and perspective ground.
When the native world replacement is active in gameplay/training frames, the
Enhanced PPU compositor also extends the surviving BG/OAM layers across the
host scene width instead of clipping them back to the centered cartridge
viewport. The old stock PPU output remains unextended; this only affects the
opt-in Enhanced presentation path.
Live Mode 2 scene frames are allowed to keep that extended native PPU output
even when the native-world replacement gate is not suppressing BG1; the
side-margin corruption guard remains for unclassified native PPU frames, not
for known-good Mode 2 terrain/background presentation.

Enhanced native scene replacement renders native shapes and the provisional
shadow pass into a transparent scratch BGRA buffer before composing PPU layers.
Mode 2 gameplay frames can still composite that native object layer when the
replacement gate is not suppressing BG1; this matches the reference presenter,
where the native Super FX layer is distinct from the SNES BG/OAM compositor.
The compositor suppresses the stock Mode 3 BG1 SuperFX world plane only when the
current source snapshot looks like a gameplay/training world frame and that
scratch render produced enough visible native pixels to replace the cartridge
framebuffer.
The gate is intentionally output-based: at least eight active source objects,
two successfully drawn native shapes, 4096 visible native pixels, and no source
text objects. It does not require a minimum source draw-list count because
runtime logs showed valid high-coverage native scenes failing solely on that
pre-render count.
Once a strong frame enters native replacement, scene-scoped hysteresis keeps
the native compositor active while at least six source objects remain and no
source text objects appear, but it now also requires at least two native shapes
and 2048 visible native pixels. This prevents ordinary low-coverage camera
moments from alternating between the wide native scene and centered stock
output without hiding the stock Super FX world behind a sparse underdrawn
native frame; UI or scene transitions still reset the replacement state.
Otherwise BG1 and the centered stock fallback remain available. This keeps the
rule conservative for title, map, briefing, and UI frames until their source
state is separately proven, and it must not re-enable Star Fox PPU/SuperFX
widening hooks.

The earlier local C Super FX shape overlay is not part of normal Enhanced
output and is no longer built into the Star Fox target. A direct bridge to the
pinned Enhanced `SoftwareRenderer` decodes Star Fox ROM shapes through
Enhanced's `ShapeDecoder` and renders them through Enhanced's mesh/material
pipeline from a read-only source-frame snapshot of retail WRAM. The native
shape path is enabled by default in Enhanced mode and can be disabled for
diagnostics with `SNESRECOMP_ENHANCED_NATIVE_SHAPES=0`.
`SNESRECOMP_ENHANCED_FRAME_BMP_DIR=<dir>` can dump bounded Enhanced
presentation-frame sequences with optional `_START`, `_END`, and `_STEP`
environment variables. The snapshot is latched at `StarFoxEnhancedPostFrame`
only when both `EnhancedRenderer` and the native-shape diagnostic gate are
enabled. Retail runtime validation currently
proves `ALLST=$121d`, `ALFREELST=$121f`, `ALBLKS=$0336`, `AL_SIZE=$36`,
`VIEWPOSX/Y/Z=$00c1/$00c3/$00c5`, `VANISHX/Y=$00ca/$00cc`,
`GAMEFRAME=$15bb`, and `WMAT11W=$161b`. The active object rows are 0x36 bytes,
but retail stores the mesh adjuncts as pointer-relative structure-of-arrays
data elsewhere in WRAM: depth offset at `object+$1cdf`, colour frame at
`object+$1ce6`, animation frame at `object+$1ce7`, colour table at
`object+$1cea`, and signed texture scroll at `object+$1cf4/$1cf5`. These
offsets are latched directly from retail WRAM; they are not derived from, and
do not require, an assumed `XALBLKS` mirror. The snapshot walks only the strict
retail active list for visible geometry; it does not reconstruct objects from
the free list, and retail has no verified `XALBLKS` mirror. Super FX draw-list
RAM is intentionally not used as visible geometry because it can be stale or
zeroed outside the source task. The shadow renderer follows Enhanced's two-pass
order and shadow-shape/flattened-matrix rules. `PLAYERFLYMODE=$1565` and
`SHADOWHEIGHT=$19dc` are latched from durable WRAM first, with the transient
Super FX mirrors kept only as fallback for task-local diagnostics. Retail
scaled-sprite objects now stay in the source draw order and use Enhanced's
simple scaled-sprite raster path,
including the source header size adjustment and per-object colour. The native
world pass also calls
Enhanced's `draw_cockpit_hud` when retail `HUDROT=$154e` is enabled, using the
source `M_HUDCOLOUR=$3512` and `M_HUDFLAGS=$3514` state and the same centered
224-pixel cockpit viewport as the PC port. The WRAM bridge is the current
Enhanced-mode renderer feed; remaining cleanup is focused on transition
artifacting, intermittent bottom-edge black bars, and particle/effect parity.

Retail and the Enhanced oracle do not share every shape header address. For
example, the live retail player object can report shape `$d320`, while the
Enhanced oracle reports `MYSHIP_4=$bb7f`; both headers point at the same
`MYSHIP_4_P/F` streams in bank `$11`, so semantic comparison must normalize
that class of ROM-data relocation before treating it as a renderer mismatch.

The renderer's ROM data symbols must also match the retail cartridge, not the
linked Enhanced build. Byte-pattern validation against the pinned reference
identified retail `SINTAB/COSTAB/SINTAB16` at
`$00:98a5/$00:98e5/$00:99e5`, `SHADESTAB2_0..3` at
`$03:8b2a/$03:8b42/$03:8b5a/$03:8b72`, `DEPTHTABLES` at `$03:8f9a`, and
`NULLSHAPE` at `$00:aca1`. Using the Enhanced-build Q15 table address made a
zero-angle object matrix non-identity and collapsed `MYBASE_0` from 17,976
visible pixels to 24. With the retail tables, native-only frame-6000 validation
draws ten real meshes and places geometry beyond both edges of the original
256-pixel viewport in a 520x224 21:9 target. A production-gated checkpoint
capture at the same frame stayed ready across the sampled late-gameplay frames
and preserved source HUD pixels, while still showing missing terrain. That
proves the object feed can produce actual wider geometry; the remaining black
and incomplete areas are missing renderer classes/composition, not widened PPU
output.

The old stock-RGB center copy remains only as a hard failure fallback if native
PPU-layer rendering cannot run or the native BG/OAM compositor detects an
unsupported wide frame with high-coverage, high-colour side-margin garbage. In
the normal Enhanced path the stock renderer is not the final image owner.

## PC Port Crosswalk

The pinned Star Fox Enhanced PC port separates simulation from presentation:
the emulated game produces state, named draw points are intercepted, and host
renderers compose a wider framebuffer from game-specific assets/state.

| PC port source | Recomp counterpart | Status |
|---|---|---|
| `src/simulation/wdc65816.cpp` symbol lookup and draw interception | `recomp/bank*.cfg` `symbol` overlay plus `StarFoxEnhancedLatchSourceFrame` feeding `StarFoxEnhancedRenderFrame` | Modified-build symbols imported; native shape snapshots now use runtime-proven retail object-list/camera addresses instead of Enhanced RAM offsets |
| `include/starfox/render/software_renderer.hpp` `RenderPose` | `StarFoxEnhancedDrawNativeShape` | Native shape bridge enabled in Enhanced mode, with diagnostics/env overrides for parity work |
| `src/render/software_renderer.cpp` shape transform, source projection, clipping, BSP ordering, face fill, simple scaled sprites | `StarFoxEnhancedDrawNativeShape` via pinned Enhanced sources | Linked and callable for solid and scaled-sprite objects from WRAM object state |
| `src/render/background_renderer.cpp` BG1/BG2/BG3 native tile composition | `src/starfox_enhanced_native.cpp` | Direct Enhanced renderer bridge for native BG layers |
| `src/render/sprite_renderer.cpp`, scaled text, particles, cockpit HUD | `src/starfox_enhanced_native.cpp` for OAM and cockpit HUD; text/effects pending | OAM and MHUD line bridge present; text/effects parity needed |
| timing interpolation in `tests/timing_tests.cpp` and simulation snapshots | presentation history, fixed duplicate-present scheduling, and native pose interpolation | Implemented for native shape poses; particle/effect parity still pending |

## Validation Rule

Any 16:9/21:9/32:9 capture with non-black garbage in the side columns is a
native renderer bug. It should be fixed in the native compositor or its Star
Fox state decode, not by re-enabling the old PPU/Super FX widescreen path.
`SNESRECOMP_ENHANCED_NATIVE_SHAPE_DIAGNOSTICS_FRAME` can restrict the verbose
per-shape pose log to one source frame during that analysis.

## Local Enhanced Oracle

For renderer-feed parity work, use a local-only `starfox-enhanced` fork with the
`starfox_tcp_oracle` dev target. It exposes Enhanced's semantic object,
draw-order, pose, PPU, render-stat, and frame-hash state over localhost TCP so
StarFoxSNESRecomp can compare against the known native renderer model. This is a
reference harness, not a production dependency and not a substitute for
Authentic-mode stock SNES PPU validation.

`tools/starfox_enhanced_oracle_compare.py` attaches to oracle-compatible TCP
endpoints and writes bounded status/object/pose/BMP snapshots for mismatches or
single-endpoint dumps.

StarFoxSNESRecomp exposes its Enhanced semantic snapshot through the trace
server's generic game-command bridge. Use the TCP debug build and prefix
semantic commands with `game `:

```powershell
$env:SNESRECOMP_DEBUG_PORT = '4582'
.\build-tcp-debug\StarFoxSNESRecomp.exe --config codex_validation_enhanced_21_9.ini --paused .\starfox.sfc

py tools\starfox_enhanced_oracle_compare.py `
  --a-port 4582 --a-prefix "game " --a-no-hello `
  --dump-only --a-run-to-frame 8000 --no-screenshot `
  --out-dir _codex_validation\semantic_smoke
```

# Star Fox Enhanced comparison

This document records what is currently useful from kandowontu's Star Fox
Enhanced decomp, <https://github.com/kandowontu/starfox-enhanced>, for
StarFoxSNESRecomp and, where applicable, for the shared snesrecomp framework.
Star Fox Enhanced remains the authoritative full-experience project and
reference implementation.

## Current local baseline

StarFoxSNESRecomp currently boots the US v1.2 game and has basic coverage for
attract, menus, route selection, training, and gameplay. Star Fox-specific
widescreen is now owned by the opt-in native renderer path documented in
`docs/TRUE_WIDESCREEN.md`; the stock PPU path remains authentic 4:3.

## SMWDisX ecosystem lesson

SuperMarioWorldRecomp already keeps its SMWDisX reference checkout as local
developer context and places the useful logic in title-owned tools:

- parse upstream symbol and bank assembly files;
- compare reference labels and mnemonic boundaries against recomp metadata;
- report missing or mismatched regions first;
- only apply excludes when the developer explicitly asks for mutation.

Star Fox should follow that shape. Enhanced is pinned as a submodule for
reproducibility, but the first integration layer is still a read-only inventory
and comparison harness rather than a bulk import into shared snesrecomp code.

## Phase burndown

1. Symbol generation and inventory: complete. The pinned UltraStarFox checkout
   generated `SYMBOLS.TXT`, and `tools/starfox_enhanced_symbols.py` parsed
   13,633 entries across ROM, WRAM, Super FX RAM, and constants/direct values.

2. Reviewed symbol promotion: complete for the first pass. See
   `docs/STARFOX_ENHANCED_SYMBOLS.md`. No `recomp/*.cfg` codegen entries were
   added because same-bank `name` directives auto-promote in snesrecomp v2 and
   the generated symbol table includes many non-code labels.

3. Mod hook identification: complete for the first pass. See
   `docs/STARFOX_ENHANCED_MOD_HOOKS.md` for crosshair color, god mode, God
   Nuke, and widescreen validation hooks.

4. snesrecomp feedback: complete for this pass. The framework already has
   frame-model documentation, frame counters/fingerprints, audio trace
   counters, and debug history. No shared code change is justified yet; the
   concrete follow-up is a title-neutral presentation diagnostics facade after
   Star Fox proves the title-side hooks.

5. Feature implementation: milestone merged. Crosshair color, God Mode, God
   Nuke, Enhanced-style display modes, persistent `ShowFPS`, `PresentationFPS`
   modes, retained-frame presentation debugging, and Enhanced native widescreen
   rendering have concrete recomp-side implementations. Enhanced-mode native
   shape poses use presentation interpolation to reduce object jitter.

6. Launcher surfacing: milestone merged. The shared recomp-ui Mods view is
   enabled for Star Fox and backed by a built-in config provider. The user-facing
   widescreen surface is a single **Enhanced Widescreen** feature with fixed
   16:10, 16:9, 21:9, and 32:9 aspect choices; disabling it returns to Authentic
   4:3.

7. Enhanced-renderer scaffold: milestone merged. The shared `RtlGameInfo`
   contract has an optional title-owned `enhanced_render_frame` hook. Star Fox
   uses it only for the opt-in Enhanced path. The direct native bridge compiles
   Star Fox Enhanced's `BackgroundRenderer`, `SpriteRenderer`, and
   `SoftwareRenderer` for BG/OAM and shape presentation. The previous local C
   Super FX shape overlay is not part of normal output.

8. Validation: in progress. Basic interactive gameplay validation and local
   TCP/frame-dump checks have passed for the merged milestone. Interactive
   route/boss audits are still required before this should be treated as full
   Enhanced parity.

## Portable candidates

1. Symbol inventory and comparison harness.
   `tools/starfox_enhanced_symbols.py` parses `SYMBOLS.TXT` output produced by
   Enhanced's upstream build flow. `tools/starfox_enhanced_compare.py` compares
   those names against local `recomp/*.cfg` function declarations without
   writing files.

2. Trusted widescreen mod path.
   Enhanced's separation of simulation timing from presentation reinforces the
   current approach: preserve the authoritative 20 Hz game state and expand only
   the rendering/projection path. The old local PPU/Super FX replay widescreen
   path has been removed from Star Fox; future widescreen work belongs in the
   native renderer.

3. Presentation and diagnostics.
   Enhanced exposes selectable presentation rates and a visible performance
   readout. Arbitrary high-FPS rendering does not transfer directly to a
   recomp, but the diagnostic model is useful for snesrecomp: frame pacing,
   simulated-frame counters, and hardware-event counters should be observable in
   a title-neutral way.

4. Small gameplay mods.
   Crosshair color, god mode, and God Nuke now exist as isolated palette, RAM,
   and input/object-list hooks. They remain config-only until route and boss
   validation catches up.

## Deferred or non-portable areas

- Enhanced's native C++ renderer is game-specific presentation code. It is not
  a drop-in replacement for the generic snesrecomp PPU renderer, but the Star
  Fox opt-in renderer can port/adapt it directly behind the title callback.
- RetroCPU, SDL3 integration, and native app structure are decomp project
  architecture, not recomp framework components.
- Star Fox EX and patch-built outputs must remain outside this repository unless
  their availability and licensing are verified separately.

## Native renderer direction

The long-term renderer plan should be split between framework and title work:

1. Add a generic snesrecomp enhanced-renderer interface that can observe frame
   boundaries, ROM/RAM state, input/config, and a title-owned projection buffer
   without weakening the default console-accurate renderer. The first hook is
   now present as an opt-in title callback from the host presentation buffer.
2. Implement Star Fox as the first opt-in plugin using reviewed symbols and the
   pinned Enhanced renderer as the source for native framebuffer, BG/OAM,
   Super FX transforms, clipping, line colors, cockpit/HUD composition, and
   high-FPS interpolation. `src/starfox_enhanced_native.cpp` now builds against
   the pinned Enhanced `BackgroundRenderer`, `SpriteRenderer`, and
   `SoftwareRenderer` and feeds them from snesrecomp state.
3. Keep the default PPU renderer intact for compatibility, debugging, and
   regression comparison.
4. Validate the native Star Fox renderer across routes, bosses, comms,
   transitions, and captures before treating it as Enhanced parity.

This is expected to be per-game tuned at the renderer plugin layer. The useful
shared work is the opt-in interface, diagnostics, frame pacing, and asset/state
handoff; the actual Star Fox mesh/projection interpretation is title-specific.

## Next validation queue

- Generate or provide `third_party/starfox-enhanced/upstream-ultrastarfox/SYMBOLS.TXT`.
- Run `python tools/starfox_enhanced_symbols.py --root .`.
- Run `python tools/starfox_enhanced_compare.py --root .`.
- Promote only reviewed names or hook points into `recomp/*.cfg` or runtime mod
  code, keeping generated ROM-derived files untracked.

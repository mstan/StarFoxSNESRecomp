# Star Fox Enhanced symbol burndown

Generated from the pinned reference chain:

- `third_party/starfox-enhanced`:
  `ad5c6e47badca6339b028c10d8371e9036ee3c79`
- `third_party/starfox-enhanced/upstream-ultrastarfox`:
  `270e959a47d82240d9290a6c6630032c9ec53ff5`

The local UltraStarFox build produced ignored artifacts:

- `third_party/starfox-enhanced/upstream-ultrastarfox/SF.SFC`
- `third_party/starfox-enhanced/upstream-ultrastarfox/SYMBOLS.TXT`
- `third_party/starfox-enhanced/upstream-ultrastarfox/BANKS.CSV`

Do not commit those generated files.

## Inventory result

`python tools/starfox_enhanced_symbols.py --root .` parsed 13,633 symbols:

| Space | Count |
|-------|------:|
| ROM | 7,947 |
| WRAM/direct-page/constants | 4,867 |
| Super FX RAM | 312 |
| WRAM banks $7E/$7F | 507 |

`python tools/starfox_enhanced_compare.py --root .` found one local cfg function:
`LoadAudio` at `$03:B109`. UltraStarFox's generated symbols do not name that
address, so keep the current StarFoxDisassembly-derived `LoadAudio` name.

## Reviewed promotions

These symbols are promoted into the reviewed hook set. This is a review
promotion, not a `recomp/*.cfg` codegen promotion. Add `func` or in-bank `name`
directives only after branch/call-boundary validation proves the target is a
real routine entry.

| Area | Symbol | Address | Space | Use |
|------|--------|---------|-------|-----|
| Frame/timing | `TRANSFER_L` | `$02:D53B` | ROM | Main 20 Hz transfer/update boundary used by Enhanced's timing analysis. |
| Diagnostics | `GAMEFRAME` | `$00:1640` | direct | Game-state frame counter candidate. |
| Diagnostics | `FRAMERATE` | `$00:156E` | direct | Source timing/rate state candidate. |
| Diagnostics | `FRAMECOUNT` | `$00:1729` | direct | Render/frame counter candidate. |
| Diagnostics | `FRAMES` | `$00:172A` | direct | Render/frame counter candidate. |
| Diagnostics | `FRAMESB` | `$00:172B` | direct | Measured frame-rate state candidate. |
| Widescreen/camera | `VIEWPOSX` | `$00:00B4` | direct | Camera X state. |
| Widescreen/camera | `VIEWPOSY` | `$00:00B6` | direct | Camera Y state. |
| Widescreen/camera | `VIEWPOSZ` | `$00:00B8` | direct | Camera Z state. |
| Widescreen/camera | `M_VANISHX` | `$70:0034` | Super FX RAM | Super FX projection vanishing point X. |
| Widescreen/camera | `M_VANISHY` | `$70:0036` | Super FX RAM | Super FX projection vanishing point Y. |
| HUD/crosshair | `CROSSHAIRON` | `$00:1633` | direct | Source flag for crosshair visibility. |
| HUD/crosshair | `ARSEBANDX` | `$00:1777` | direct | Crosshair X state in UltraStarFox symbols. |
| HUD/crosshair | `ARSEBANDY` | `$00:1779` | direct | Crosshair Y state in UltraStarFox symbols. |
| HUD/crosshair | `DO_CROSSHAIR` | `$03:D4CE` | ROM | Candidate crosshair draw routine. Requires call-boundary validation. |
| HUD/crosshair | `M_HUDCOLOUR` | `$70:3512` | Super FX RAM | Super FX HUD/cockpit color input used by Enhanced rendering. |
| HUD/meters | `M_DAMAGE` | `$70:018C` | Super FX RAM | Player damage meter source. |
| HUD/meters | `M_SHIELDUP` | `$70:0190` | Super FX RAM | Shield meter/source flag. |
| HUD/meters | `GAMEPALBUFF` | `$7E:81EF` | WRAM | Game palette buffer used by Enhanced palette extraction. |
| God mode | `PSHIPFLAGS3` | `$00:1563` | direct | Enhanced sets bit `0x08` to preserve no-collision. |
| God mode | `SPECWEPCNT` | `$00:1634` | direct | Nova Bomb count floor while god mode is active. |
| God mode | `SPECIALDELAY` | `$00:15B1` | direct | God Nuke shortens post-fire bomb cadence. |
| God mode | `PCBOXOBJ_B` | `$00:15F2` | direct | Player body collision-object pointer. |
| God mode | `PCBOXOBJ_LW` | `$00:15EE` | direct | Player left-wing collision-object pointer. |
| God mode | `PCBOXOBJ_RW` | `$00:15F0` | direct | Player right-wing collision-object pointer. |
| God mode | `PLAYERB_HP` | `$00:0028` | direct | Player body collision shape HP constant. |
| God Nuke | `ALBLKS` | `$00:0338` | direct | Object pool base used by the active-list walker. |
| God Nuke | `AL_SIZE` | `$00:0038` | direct | Retail object record size. |
| God Nuke | `NUMBER_AL` | `$00:0046` | direct | Retail object count. |
| God Nuke | `ALLST` | `$00:12AD` | direct | Active object list head. |
| God Nuke | `AL_SHAPE` | `$00:0004` | direct | Object shape field offset. |
| God Nuke | `AL_TYPE` | `$00:0009` | direct | Object type field offset; Enhanced sets the nuked bit. |
| God Nuke | `AL_STRATPTR` | `$00:0016` | direct | Three-byte strategy pointer field offset. |
| God Nuke | `AL_SFLAGS` | `$00:001D` | direct | Strategy flag byte; Enhanced sets hit flash. |
| God Nuke | `AL_SFLAGS2` | `$00:001E` | direct | Strategy flag byte; Enhanced disables collision for autokilled objects. |
| God Nuke | `AL_HP` | `$00:002A` | direct | Object health byte. |
| God Nuke | `AL_COLLFLAGS` | `$00:002E` | direct | Collision flags; friend-collision objects are skipped. |
| God Nuke | `NUKE` | `$00:97A0` | ROM | Source shape symbol, ambiguous with data/name alias. |
| God Nuke | `NUKE` | `$11:EDB4` | ROM | Runtime shape/address alias seen in symbols. |
| God Nuke | `FIRE_NUKE` | `$21:A784` | ROM | Candidate normal nuke fire path. Requires call-boundary validation. |
| God Nuke | `NUKEEXP_STRAT` | `$21:A373` | ROM | Nuke explosion strategy used by Enhanced for detonation detection. |
| God Nuke | `NUKE_ISTRAT` | `$21:A241` | ROM | Nuke initializer strategy. |
| God Nuke | `NUKE_STRAT` | `$21:A2B4` | ROM | Nuke active strategy. |
| God Nuke | `BOSS_2_0`..`BOSS_2_5` | `$00:A6EF`..`$00:A77B` | ROM | Macbeth boss components receive regular nuke damage only. |
| God Nuke | `ANDROSS` | `$00:94AC` | ROM | Protected from autokill. |
| God Nuke | `ANDROSSCUBE` | `$00:94E4` | ROM | Protected from autokill. |

## Cfg policy

`python tools/ingest_starfox_enhanced_symbols.py --root .` overlays the 7,947
ROM symbols into `recomp/bank*.cfg` as `symbol <pc24> <name>` lines. The normal
`tools/regen.sh` path runs this ingester before `v2_emit.py`, so the actual
recomp consumes the pinned Star Fox Enhanced names during analysis and emission.

These are deliberately not `func` or same-bank `name` directives. Star Fox
Enhanced's generated table includes routine labels, branch labels, Super FX
MARIO labels, shape headers, tables, text, and constants. In snesrecomp v2,
same-bank `name` entries promote to emitted functions; blindly promoting this
table would create bogus 65816 roots. `symbol` is a non-promoting overlay:
it names an address when real analysis reaches it, while reachability and
function boundaries remain under the recompiler's control.

Hand-authored `func`, `name`, and `symbol` declarations outside the auto block
win by address. The auto block is replaced wholesale on each ingest run.

# Arwing64 source/host seam map

Retail Star Fox (USA) v1.2 addresses that the Arwing64 mod (Star Fox 64 Arwing
rendered in place of the Super FX player ship) reads or patches. Every address
below was byte-verified against the retail ROM (`starfox.sfc`, 1 MiB LoROM) on
2026-09-03; the UltraStarFox / Star Fox Enhanced sources supply semantics only,
their build addresses differ (WRAM block `$14C2..$15A2` is offset by `$8B`).

Sources: `_refs/StarFoxDisassembly` (retail, SpyderTL), UltraStarFox
`SF/INC/GILESALC.INC`, `SF/INC/SOUNDEQU.INC`, `SF/STRAT/GSTRATS.ASM`,
`SF/ASM/SOUND.ASM`, `SF/STRAT/STRATROU.ASM`, `SF/ASM/COLDET.ASM`.

## Player shape selection and the invisible-player patch

| Item | Retail | Notes |
|---|---|---|
| `ArwingModelIDTable` (`player_shapes`) | `$06:80D5`, ROM `0x300D5` | 7 rows x `dw normal, noLeft, noRight, noWings`; row 0 = `$D320 $D3AC $D374 $D3E4` (MYSHIP_4 / _R / _L / _B) |
| `SetArwingModel` (`setYplayershape_l`) | `$06:810D` | reads `$14D6 & $18` to pick the slot |
| `DoInitArwingModelBuffer` (`select_ship`) | `$06:8191` | copies the row to `$7E:3186..318C` (`playershape/L/R/LR`) |
| `setcurrpshape` (per-frame `al_shape` writer) | `$0B:EB28` | writes `$70:2B26` wing state 0..3 and `al_shape`; in cockpit view (`$14DB == 3`) swaps `al_shape` to `nullPlayer` |
| `nullPlayer` shape header | `$00:D2CC`, ROM `0x052CC` | byte-identical to MYSHIP_4 from `sh_radius` onward, geometry pointers zero: the game's own invisible Arwing |
| `NULLSHAPE` | `$00:ACA1` | NOT gameplay neutral for the player (collision box 136/136/144, size 188, shift 2) |
| MYSHIP_4 / _L / _R / _B headers | `$00:D320 / D374 / D3AC / D3E4` | 28-byte headers; `$D320` is also `ModelIDTable[2]`, do not patch the headers themselves |

Collision reads the shape header (`sh_radius, sh_xmax, sh_ymax, sh_zmax` ->
`cl_*`, STRATROU.ASM:58-72); sort uses `sh_sortz`; explosions use `sh_size`.
Therefore the guarded ROM patch writes `cc d2 cc d2 cc d2` over ROM
`0x300D5..0x300DA` (row 0 slots +0/+2/+4 -> `nullPlayer`). Slot +6 (both
wings gone, `$D3E4`, xmax 20) is left stock; the host draws the both-broken
variant over it and the GSU ship there is a 16-vertex stub that the host
model fully covers. Only `SetArwingModel` / `DoInitArwingModelBuffer` read
this table.

MYSHIP_4 geometry (shift 0, +Z forward, +Y down): 16 vertices, bbox
x -36..36, y -11..14, z -40..80; header half-extents 36/14/80, size 80.
Used to fit the N64 model: N64 Arwing wingspan maps to 72 units.

Shape header (28 bytes): `+0 sh_points(2) +2 sh_bank +3 sh_faces(2)
+5 sh_sortz(2) +7 sh_shift +8 sh_radius(2) +A xmax +C ymax +E zmax +10 size
+12 col_ptr +14 shadow +16 lod1 +18 lod2 +1A lod3`.

## Player motion state

| Feature | Retail | Width | Semantics |
|---|---|---|---|
| `pshipflags` | `$14D6` | 1 | b3 `$08` LEFT wing gone, b4 `$10` RIGHT wing gone, b0 body hit, b1 L hit, b2 R hit, b5 noctrl, b6 nofire |
| `pshipflags2` | `$14D7` | 1 | b0 twin laser, b1 wireship (shield), b5 `$20` boosting, b6 `$40` braking, b7 `$80` shield empty |
| `pshipflags3` | `$14D8` | 1 | b1 engine sound on, b3 no collisions, b4 beam-ball laser |
| wing state (GSU mirror) | `$70:2B26` | 2 | 0 both, 1 left gone, 2 right gone, 3 both gone |
| `playerflymode` | `$14DA` (GSU mirror `$70:01A0`) | 1 | b0 diefall, b1 dieYrot, b2 water, b3 shadows, b4 wobble |
| `CurViewMode` | `$14DB` | 1 | 0 far 3rd person, 1 close 3rd person, 2 to-cockpit, 3 COCKPIT, 4 to-normal |
| boost meter anim | `$70:01BC` | 2 | 40 full, -2/frame boosting, +1/frame recovering |
| boost charge | `$70:01BA` | 2 | nonzero while boost/brake burning |
| engine sound flag | `$1F43` | 1 | `$04` normal, `$08` boosting, `$0C` braking |
| barrel roll velocity | `$1501` | 1 signed | nonzero = rolling; set to -32 / +32 at start, decays 2/frame |
| barrel roll offset | `$1502` | 1 signed | added to `al_rotz` each frame |
| roll delay | `$1500` | 1 | cooldown |
| shield / HP | `$70:01B8` (source `[$1567]+$2A`) | 2 | damage meter |
| shield power-up | `$16CD` | 1 | nonzero while shield item active |
| damage flash | `$1527` / `$1528` | 1 / 1 | frames / type |
| nova bombs | `$15AD` | 2 | count |
| shots alive | `$1523` | 1 | max 4 single / 8 twin |
| `gameflags` | `$14D0` | 1 | b1 player dying, b6 player dead, b7 stage done |
| `gameflags2` | `$14D1` | 1 | b3 in game |
| player object ptr | `$1238` | 2 | `al_shape +$04`, `al_type +$09`, `al_sflags +$1D`, `al_HP +$2A`, `al_collflags +$2E`; record `$38`, 70 slots from `$0336` |
| camera | `$14F6 / $14F8 / $14FA` | 2 each | pviewpos x/y/z |
| player world pos | `$150D / $150F / $1511` | 2 each | |
| player speed | `$1509` | 2 | |

## Sound trigger protocol

| Port | Reg | Carries | Arwing64 use |
|---|---|---|---|
| APUIO0 | `$2140` | music ID + upload handshake | ignore |
| APUIO1 | `$2141` | engine byte, one write per frame | engine loop state |
| APUIO2 | `$2142` | ambient selector | ignore |
| APUIO3 | `$2143` | ONE SFX ID per write from a 16-entry ring | SFX cue source |

`PushSoundEffectToQueue` = `$03:B7F9` (`lda #id : jsl $03B7F9`, 320 sites);
`UpdateMusic` = `$02:8E8E` writes the queue head to `$2143` and mirrors it in
`$1F51`, then clears both when the SPC echoes it. Queue `$1F53` (16 bytes),
write index `$1F4D`, read index `$1F4F`, mute-all `$1FCF`.

Engine byte on `$2141`: `$00` off; `$4B` low-shield alarm; else
`base | $1F43 | accel` with base `%11000000` planet/water, `%10000000`
tunnel, `0` space; accel 0..3 in bits 0-1 only while L/R held (barrel roll).

SFX IDs on `$2143`:

| SFX | ID | Call site |
|---|---|---|
| player laser | `$35` | `$0B:E123` |
| twin laser | `$34` | `$0B:E181` |
| beam-ball laser | `$36` | `$0B:E1DC` |
| nova bomb launch | `$31` | `$0B:E04D` |
| nova bomb explode | `$30` | `$06:A364` |
| boost | `$32` | `$0B:D958` |
| brake | `$33` | `$0B:D980` |
| body hit heavy / light | `$04` / `$19` | `$0B:AE93` / `$0B:AE9B` |
| left wing hit | `$04` / `$07` | `$0B:B13B` / `$0B:B143` |
| left wing lost | `$05` | `$0B:B263` |
| right wing hit | `$04` / `$08` | `$0B:B3DA` / `$0B:B3E2` |
| right wing lost | `$06` | `$0B:B502` |
| player explosion / death | `$03` | `$0B:E30F`, `$0B:E3F7`, `$0B:E6FC` |
| shield deflect ping | `$14` | `$0B:AD85`, `$0B:E742` |
| shield 25% / 12.5% warning | `$1B` / `$1C` | `$0B:AF6A` / `$0B:AF92` |

Barrel roll has no queued SFX; it is inferred from `$1501`.

## Enhanced renderer constants that are UltraStarFox addresses (wrong for retail)

Found while proving the seams; tracked under beads-8wg.8.2. The listed retail
address corrections were applied on the Arwing64 branch on 2026-09-05. The
"Current" column below records the previous UltraStarFox values for comparison.

| Constant | Current | Retail |
|---|---|---|
| `kRamHudRotation` | `0x154e` | `$14C3` |
| `kRamPlayerFlyMode` | `0x1565` | `$14DA` |
| `kRamShieldUp` | `0x1752` | `$16CD` |
| `kGsuPlayerFlyMode` | `0x0174` | `$70:01A0` |
| `kGsuBossMaxHp` / `kGsuBossHp` | `0x016e` / `0x0170` | `$70:019A` / `$70:019C` |
| `kGsuDamage` | `0x018c` | `$70:01B8` |
| `kGsuBoostAnim` | `0x018e` | `$70:01BC` |
| `kGsuShieldUp` | `0x0190` | no GSU equivalent; WRAM `$16CD` |
| `kGsuHudColour` / `kGsuHudDamageFlags` | `0x3512` / `0x3514` | `$70:2B24` / `$70:2B26` |

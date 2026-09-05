# Star Fox 64 Arwing preview

Arwing64 replaces the player presentation with the Star Fox 64 Arwing, wing
damage variants, engine glow, roll shield, and ship sound effects. It works with
Authentic 4:3 and Enhanced widescreen. It is an opt-in development preview:
the strict gameplay-state comparison is still under investigation and owner
playtesting is pending. Do not treat this branch as release-approved.

## Enable it

In the launcher's Mods page, choose **Star Fox 64 Arwing**, select your own
**Star Fox 64 (USA) Rev A / v1.1** ROM, then enable the feature. Anti-aliasing
and Star Fox 64 audio are separate options. The expected normalized ROM SHA-1
is `09f0d105f476b00efa5303a3ebc42e60a7753b7a`. Native `.z64`, byte-swapped `.v64`,
and word-swapped `.n64` dumps are accepted; other revisions are rejected.

Equivalent settings, alongside the ordinary Star Fox SNES ROM selection:

```ini
[Features]
Arwing64 = 1
Arwing64Rom = C:\Games\MyRoms\StarFox64.z64
Arwing64Supersample = 2
Arwing64Sfx = 1
```

The feature defaults off. First activation extracts assets into
`arwing64_cache` beside the executable; later activations verify and reuse
them. Keep this directory local. It contains Nintendo-derived mesh, textures,
and audio and must never be committed, shared, or bundled in a release.

Missing or incorrect ROMs, invalid caches, unavailable audio clips when audio
is enabled, and ROM patch mismatches leave the stock presentation active. A
corrupt committed cache is rejected rather than silently accepted. To rebuild,
close the game, remove your local `arwing64_cache` directory, and restart with
the correct ROM selected. Disabling the feature restores the ROM patch and
unloads its audio. Music, comms, ambient sounds and unmapped SFX remain SNES.

## What the audio renderer does

Fourteen stereo PCM-16 WAVs at 32040 Hz cover single/twin/hyper lasers, bomb
launch/explosion, boost, brake, wing/body damage, wing loss, ship explosion,
shield deflection, engine hum and barrel roll. They come from player-bank
sequence scripts and instruments in the owner ROM, including VADPCM predictor
books, loop history, note velocity, envelopes and pitch sweeps. A versioned
manifest records each WAV's SHA-256. This is a dry host rendering, not a
bit-exact capture of the N64 mixer: spatial effects and reverb are omitted,
planet variants are selected, and long one-shot scripts are capped at eight
seconds. Flap motion and damage-flash colours are presentation approximations.

SNES `$2143` requests map to SF64 cues. A successfully replaced request is
consumed; the host supplies its acknowledgement so Star Fox's sound queue can
continue. Repeated writes of a pending ID are not played twice. Engine requests
use `$2141`; the low-shield alarm passes through. Barrel-roll audio follows the
rising edge of the guest's roll state. Reset/load clears host delivery state,
and mission exit stops the engine loop. The mod does not write WRAM.

## Development and verification

Build `StarFoxSNESRecomp` and `arwing64_tool` with native MinGW executables from
PowerShell. Do not regenerate or edit `src/gen` for this feature.

```powershell
& C:\msys64\mingw64\bin\cmake.exe --build build-arwing --target StarFoxSNESRecomp arwing64_tool -j 12
$env:ARWING64_TOOL = "$pwd/build-arwing/arwing64_tool.exe"
$env:SF64_ROM = 'C:/Games/MyRoms/StarFox64.z64'
$env:SF64_DECOMP = 'C:/Source/sf64'
python -m unittest discover -s tests -p 'test_arwing64*.py' -v
```

Use a native Python executable, not an MSYS Python shim. The differential
test compiles the decomp's actual `tools/aifc_decode.c` locally and compares
17 decoded instruments byte for byte; it also checks WAVs, manifest hashes,
loop markers and extraction repeatability. Synthetic tests exercise the cue
protocol, host mixer and lifecycle without an owner ROM. Mesh tests compare
the per-display-list triangle census with Torch's extracted C output.

For local diagnostics, create an output directory and run:

```text
arwing64_tool <owner-rom> <output-directory> --json
arwing64_tool <owner-rom> <output-directory> --audio
arwing64_tool <owner-rom> <output-directory> --sample 0
```

The last command writes raw little-endian PCM for the developer differential.
TCP debug builds expose `game arwing status`, `state`, `cache`, `patch`, and
`audio`. Presentation-only overrides are available with
`game arwing force wing=1 roll=32 boost=1` and `game arwing force clear`.
For scripted launches the SNES ROM must be the final positional argument or
the launcher can wait for ROM selection.

## Validation status and limits

On 2026-09-05, the native audio differential and cue/lifecycle tests passed;
the mesh census matched Torch. Two repeated stock runs matched full 128 KiB
WRAM at all 16 checkpoints from guest frames 5000 through 6500. Arwing64 with
audio on and off also matched at every checkpoint. However, stock versus the
Arwing64 visual path differed beyond the expected shape references, including
player/camera positions. An isolation build that still draws the host Arwing
but retains the stock ROM table matched stock WRAM exactly at every checkpoint.
This identifies the existing guest-visible hide patch as the cause. That
strict faithfulness gate remains open under
`beads-8wg.8.3.5`; passing visual screenshots is not proof of gameplay parity.

The guarded patch changes only six ROM bytes at `0x300D5`; the SNES program
then copies changed shape references into its own state. See
[the source seam map](ARWING64_SOURCE_SEAMS.md) for the retail addresses and
collision/header evidence. With both wings gone the original reduced GSU
stub remains underneath the host mesh. Enhanced transition fallback and
broader effects parity remain separate development limitations.

The engine branch remains based on `a595a41` pending the engine-main regression
tracked in `beads-8wg.2.27`. Do not rebase this preview onto the regressed main.
No source branch has been pushed or submitted as a PR for this finish pass.
Authentic and Enhanced scripted runs through frame 9500 both exited cleanly,
with 14 clips loaded, 23 mapped requests consumed and a roll cue played.
Audio on/off full-WRAM comparisons also matched all 19 gameplay checkpoints
from frames 7400 through 9200 while those cues and roll were exercised.
Corrupted WAV, corrupted mesh and missing-ROM runtime probes all kept the patch
off and audio disabled. The local preview ZIP passed packaging; synthetic
negative tests rejected nested caches, extracted WAV names, ROM names and a
renamed mesh blob. These passes do not waive the state-parity release blocker.

Before release: resolve strict state parity, exercise full Corneria runs in
both presentation modes (including damage, wing loss, roll, boost/brake,
cockpit and reset/load), obtain the owner's visual/audio verdict, and run
`tools/make_release.ps1`. Packaging excludes the cache and extractor tool and
rejects nested owner ROMs, extracted WAVs and mesh blobs. The release config
always disables Arwing64 and clears the developer's SF64 ROM path.

## Credits

* [sonicdcer/sf64](https://gitlab.com/sonicdcer/sf64), CC0: source semantics
  for the asset formats, display lists, audio sequence interpreter and decoder.
* [HarbourMasters/Torch](https://github.com/HarbourMasters/Torch), MIT,
  copyright 2023 Lywx: development-only extraction oracle; not embedded.
* [kandowontu/Star Fox Enhanced](https://github.com/kandowontu/starfox-enhanced):
  reference implementation for the native world renderer and presentation.
* Nintendo owns Star Fox, Star Fox 64, their models, textures and sounds. None
  of those extracted assets is included in this source repository or release.

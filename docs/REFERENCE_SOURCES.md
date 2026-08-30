# Reference sources

## Star Fox US v1.2 disassembly

- Repository: <https://github.com/SpyderTL/StarFoxDisassembly>
- Pinned reference commit: `20f9f3c77f453945d81906a9499a136818f93857`
- Target: Star Fox SNES, US version 1.2

The upstream repository does not declare a license and says the disassembly is
not yet rebuildable. It is therefore used only as a local read-only source of
addresses, labels, comments, and behavioral annotations. Its assembly and
assets are not vendored into this project.

## Star Fox Enhanced / UltraStarFox decomp references

- Repository: <https://github.com/kandowontu/starfox-enhanced>
- Pinned reference commit: `ad5c6e47badca6339b028c10d8371e9036ee3c79`
- Local path: `third_party/starfox-enhanced`
- Original-game upstream declared by Enhanced:
  <https://github.com/Sunlitspace542/ultrastarfox.git> at
  `270e959a47d82240d9290a6c6630032c9ec53ff5`
- Star Fox EX upstream declared by Enhanced:
  <https://github.com/sunlitspace542/star-fox-ex.git> at
  `b5e2d837a15a72a532cd019bfe332b7a4b660924`

The Enhanced repository is pinned as a reference-only submodule. Use it for
provenance, symbol names, architecture notes, timing/rendering validation
ideas, and portable mod candidates. Do not copy generated ROM data, BPS patch
outputs, prebuilt binaries, or reconstructed game assets into this repository.
The StarFoxSNESRecomp widescreen renderer is adapted from this project's
separate native renderer and `DisplayMode` model; attribution for that
widescreen design and reference implementation belongs to kandowontu's
Star Fox Enhanced project.

The Star Fox EX upstream listed above is recorded from Enhanced's
`config/upstream-ex.json`. Verify availability before relying on it for new
work, since the declared remote may be private, renamed, or unavailable.

## Super FX behavioral references

The shared engine implementation should be derived from public hardware
documentation and suitably licensed emulator implementations, with provenance
recorded alongside any incorporated code.

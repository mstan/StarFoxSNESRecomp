#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
PYTHON="${PYTHON:-$(command -v python3 || command -v python)}"
test -f starfox.sfc || { echo "stage verified starfox.sfc first" >&2; exit 1; }
"$PYTHON" snesrecomp/tools/v2_emit.py --rom starfox.sfc \
  --cfg-dir recomp --out-dir src/gen --no-host-root-scan
"$PYTHON" snesrecomp/tools/v2_sync_funcs_h.py --cfg-dir recomp --out recomp/funcs.h

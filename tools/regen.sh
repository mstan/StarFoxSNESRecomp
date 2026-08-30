#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
PYTHON="${PYTHON:-$(command -v python3 || command -v python)}"
ANALYSIS_BACKEND="${SNESRECOMP_ANALYSIS_BACKEND:-native}"
SNESRECOMP_ROOT="${SNESRECOMP_ROOT:-snesrecomp}"

case "$ANALYSIS_BACKEND" in
  native|python|auto) ;;
  *)
    echo "regen.sh: invalid SNESRECOMP_ANALYSIS_BACKEND: $ANALYSIS_BACKEND" >&2
    exit 2
    ;;
esac

if [ "$ANALYSIS_BACKEND" = native ]; then
  "$PYTHON" "$SNESRECOMP_ROOT/tools/build_native_analyzer.py"
fi

"$PYTHON" tools/ingest_starfox_enhanced_symbols.py --allow-missing

test -f starfox.sfc || { echo "stage verified starfox.sfc first" >&2; exit 1; }
# Every declared function is a static-analysis root. Unprovable variants still
# fall back to the interpreter; --cfg-roots only prevents declared coverage
# from depending on host references or a runtime profile.
"$PYTHON" "$SNESRECOMP_ROOT/tools/v2_emit.py" --rom starfox.sfc \
  --cfg-dir recomp --out-dir src/gen --cfg-roots --no-host-root-scan \
  --analysis-backend "$ANALYSIS_BACKEND"
"$PYTHON" "$SNESRECOMP_ROOT/tools/v2_sync_funcs_h.py" \
  --cfg-dir recomp --out recomp/funcs.h

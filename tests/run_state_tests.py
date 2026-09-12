"""Run ROM-dependent menu/replay checks in a fresh directory, then restart."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("executable", type=Path)
parser.add_argument("rom", type=Path)
args = parser.parse_args()
exe, rom = args.executable.resolve(strict=True), args.rom.resolve(strict=True)
options = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
with tempfile.TemporaryDirectory(prefix="starfox-state-test-") as cwd:
    for extra in ([], ["--resume"]):
        result = subprocess.run([str(exe), str(rom), *extra], cwd=cwd,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, timeout=180, **options)
        print(result.stdout, end="", flush=True)
        result.check_returncode()
        marker = ("new-process replay matches original execution" if extra
                  else "All Star Fox state/menu checks passed.")
        if marker not in result.stdout:
            raise RuntimeError("State harness exited without completing its checks")

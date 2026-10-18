"""Check finite launches and valued-argument ordering against the actual host."""
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
env = dict(os.environ, SDL_AUDIODRIVER="dummy", SDL_AUDIO_DRIVER="dummy",
           SDL_VIDEODRIVER="dummy", SDL_VIDEO_DRIVER="dummy")
options = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
with tempfile.TemporaryDirectory(prefix="starfox-cli-") as directory:
    root = Path(directory)
    config = root / "config.ini"
    config.write_text("[General]\nSkipLauncher=1\n[Graphics]\nOutputMethod=SDL-Software\n"
                      "NewRenderer=1\n[Sound]\nEnableAudio=1\n", encoding="utf-8")
    orders = (["--frames", "8", str(rom)], [str(rom), "--frames", "8"],
              ["--frames", "8", "--rom", str(rom)])
    for order in orders:
        result = subprocess.run([str(exe), *order, "--config", str(config)],
                                cwd=root, env=env, capture_output=True, text=True,
                                timeout=30, **options)
        if result.returncode or "FATAL" in result.stderr:
            raise RuntimeError(result.stdout + result.stderr)
        print("PASS: finite launch order", order, flush=True)
    for value in ("missing", "-1", "4294967296", "1x"):
        flags = [] if value == "missing" else [value]
        result = subprocess.run([str(exe), "--frames", *flags], cwd=root, env=env,
                                capture_output=True, text=True, timeout=15, **options)
        if result.returncode != 2:
            raise RuntimeError(f"invalid count {value}: {result.returncode}")
    print("PASS: malformed and overflowing frame counts are rejected")

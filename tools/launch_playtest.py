"""Launch a local playtest with direct file handles for stdout/stderr.

PowerShell Start-Process -RedirectStandardError caused blocking writes on the
owner's Windows machine (including multi-second game startup stalls). Keep
logging out of the PowerShell redirection path. No ROM or configuration is
modified; all arguments after the executable are forwarded unchanged.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log-prefix", type=Path, required=True)
    parser.add_argument("--wait", action="store_true",
                        help="wait for exit and return the game's exit code")
    parser.add_argument("--hidden", action="store_true",
                        help="request a hidden window for scripted checks")
    parser.add_argument("executable", type=Path)
    parser.add_argument("arguments", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    executable = args.executable.resolve(strict=True)
    prefix = args.log_prefix.resolve()
    prefix.parent.mkdir(parents=True, exist_ok=True)
    stdout_path = Path(str(prefix) + ".stdout.log")
    stderr_path = Path(str(prefix) + ".stderr.log")
    arguments = args.arguments
    if arguments[:1] == ["--"]:
        arguments = arguments[1:]
    options = {}
    if os.name == "nt":
        # The game/launcher creates its own GUI; do not create a console.
        options["creationflags"] = subprocess.CREATE_NO_WINDOW
        if args.hidden:
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = 0
            options["startupinfo"] = startup
    with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
        process = subprocess.Popen([str(executable), *arguments],
                                   stdout=stdout, stderr=stderr, **options)
    print(json.dumps({"pid": process.pid, "stdout": str(stdout_path),
                      "stderr": str(stderr_path)}), flush=True)
    return process.wait() if args.wait else 0


if __name__ == "__main__":
    raise SystemExit(main())

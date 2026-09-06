"""Compare original guest state with Arwing64 off/on using a TCP debug build.

Requires owner ROMs and an input script; keeps local configs, logs and state hashes.
The configured executable must have the debug server on localhost:4381.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import time


def capture(args, enabled):
    label = 'on' if enabled else 'off'
    config = args.output / (label + '.ini')
    text = args.config.read_text()
    text, count = re.subn(r'(?mi)^Arwing64\s*=.*$', 'Arwing64 = '+str(int(enabled)), text)
    if count != 1:
        raise RuntimeError('config must contain exactly one Arwing64 setting')
    config.write_text(text)
    env = os.environ.copy()
    env['SDL_AUDIODRIVER'] = 'dummy'
    startup = None
    if os.name == 'nt':
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
    with (args.output / (label + '.log')).open('w') as log:
        process = subprocess.Popen([str(args.exe), '--config', str(config), '--script',
                                    str(args.script), '--frames', str(max(args.frames)+30),
                                    str(args.snes_rom)], cwd=args.exe.parent,
                                   env=env, stdout=log, stderr=log, startupinfo=startup)
        sock = None
        deadline = time.monotonic()+600
        try:
            while process.poll() is None and time.monotonic() < deadline:
                try:
                    sock = socket.create_connection(('127.0.0.1', 4381), timeout=1)
                    break
                except OSError:
                    time.sleep(.1)
            if sock is None:
                raise RuntimeError('debug server did not start')
            sock.settimeout(15)
            reader = sock.makefile('rb')

            def query(command, raw=False):
                sock.sendall((command+'\n').encode())
                value = reader.readline().decode().strip()
                if raw:
                    return value
                data = json.loads(value)
                if 'error' in data:
                    raise RuntimeError(data['error'])
                return data

            result = {}
            for frame in args.frames:
                # record_frame runs before the engine increments its public
                # counter. Wait for that increment, after the frame completes.
                query('run_to_frame '+str(frame-1))
                while True:
                    current = query('frame')['frame']
                    if current == frame:
                        break
                    if current > frame or process.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError(f'failed to pause at frame {frame}, current {current}')
                    time.sleep(.05)
                snapshot = {}
                for name, cmd in [('wram', 'dump_ram 0 131072'),
                                  ('gsu_ram', 'read_sram 0 65536'),
                                  ('vram', 'dump_vram 0 65536')]:
                    data = bytes.fromhex(query(cmd)['hex'])
                    snapshot[name] = hashlib.sha256(data).hexdigest()
                snapshot['gsu'] = query('get_superfx_state')
                if query('frame')['frame'] != frame:
                    raise RuntimeError('guest advanced while capturing paused state')
                result[str(frame)] = snapshot
                print(label, frame, 'captured WRAM, GSU RAM/state, VRAM', flush=True)
            status = {name: query('game arwing '+name, raw=True)
                      for name in ('status', 'picture', 'audio')}
            query('continue')
            process.wait(timeout=30)
            if process.returncode:
                raise RuntimeError(f'game exited with {process.returncode}')
            (args.output / (label+'.json')).write_text(json.dumps({'frames': result, 'mod': status}, indent=2))
            return result
        finally:
            if sock:
                sock.close()
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=10)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('exe', 'config', 'script', 'snes-rom', 'output'):
        parser.add_argument('--'+name, type=lambda p: Path(p).resolve(), required=True)
    parser.add_argument('--frames', type=int, nargs='+', default=[5000,5300,6200,6500,7600,8100,9200])
    args = parser.parse_args()
    if args.frames != sorted(set(args.frames)) or min(args.frames) < 100:
        parser.error('frames must be unique, increasing and at least 100')
    args.output.mkdir(parents=True, exist_ok=True)
    off, on = capture(args, False), capture(args, True)
    differences = {frame: [key for key in off[frame] if off[frame][key] != on[frame][key]]
                   for frame in off if off[frame] != on[frame]}
    (args.output/'comparison.json').write_text(json.dumps(differences, indent=2))
    if differences:
        raise SystemExit('FAIL: '+json.dumps(differences))
    print(f'PASS: {len(off)} checkpoints, full WRAM/GSU RAM/VRAM hashes and GSU state identical')


if __name__ == '__main__':
    main()

"""Reject local ROMs and extracted Arwing assets before publishing a package."""
from pathlib import Path
import re
import sys


def check_payload(root):
    count = 0
    for path in root.rglob('*'):
        if not path.is_file():
            continue
        relative = path.relative_to(root).as_posix()
        if re.search(r'(?i)(^|/)arwing64_cache(/|$)|(^|/)sfx_[^/]*\.wav$|'
                     r'\.(z64|v64|n64|sfc|smc|rom|pcm|adpcm)$', relative):
            raise ValueError(f'Owner ROM or extracted asset in release stage: {relative}')
        with path.open('rb') as stream:
            if stream.read(8) == b'N64MESHB':
                raise ValueError(f'Extracted host mesh in release stage: {relative}')
        count += 1
    if not count:
        raise ValueError('Release stage is empty')
    return count


if __name__ == '__main__':
    print(f'Release payload: {check_payload(Path(sys.argv[1]))} files checked')

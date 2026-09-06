import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class PresentationTests(unittest.TestCase):
    def test_visible_frame_and_adaptive_composition(self):
        cc = pathlib.Path('C:/msys64/mingw64/bin/cc.exe')
        compiler = str(cc) if cc.exists() else shutil.which('cc')
        if not compiler:
            self.skipTest('native C compiler unavailable')
        with tempfile.TemporaryDirectory() as tmp:
            exe = pathlib.Path(tmp) / 'presentation.exe'
            args = [compiler, '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
                    '-I'+str(ROOT/'src'),
                    '-I'+str(ROOT/'snesrecomp/runner/src'),
                    str(ROOT/'tests/starfox_presentation_runtime.c'),
                    str(ROOT/'src/starfox_presentation.c'), '-o', str(exe)]
            result = subprocess.run(args, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

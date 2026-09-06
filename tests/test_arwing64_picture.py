import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CC = pathlib.Path('C:/msys64/mingw64/bin/cc.exe')


class PictureRuntimeTests(unittest.TestCase):
    def test_private_picture_and_generation_matching(self):
        cc = str(CC) if CC.exists() else shutil.which('cc')
        if not cc:
            self.skipTest('native C compiler unavailable')
        with tempfile.TemporaryDirectory() as tmp:
            exe = pathlib.Path(tmp) / 'picture_test.exe'
            args = [cc, '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
                    '-I'+str(ROOT/'src'), '-I'+str(ROOT/'snesrecomp/runner/src'),
                    str(ROOT/'tests/arwing64_picture_runtime.c'),
                    str(ROOT/'src/mods/arwing64/arwing64_picture.c'),
                    str(ROOT/'snesrecomp/runner/src/snes/superfx.c'), '-o', str(exe)]
            result = subprocess.run(args, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()

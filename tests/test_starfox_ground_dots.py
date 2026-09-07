import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class GroundDotsTests(unittest.TestCase):
    def test_projection_matches_reference(self):
        mingw = pathlib.Path('C:/msys64/mingw64/bin')
        compiler = str(mingw / 'c++.exe') if mingw.exists() else shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler unavailable')
        vendor = ROOT / 'third_party/starfox-enhanced'
        with tempfile.TemporaryDirectory() as tmp:
            exe = pathlib.Path(tmp) / 'ground_dots.exe'
            args = [compiler, '-std=c++20', '-O1', '-I' + str(ROOT / 'src'),
                    '-I' + str(vendor / 'include'),
                    str(ROOT / 'tests/starfox_ground_dots.cpp'),
                    str(vendor / 'src/render/dust_renderer.cpp'),
                    str(vendor / 'src/simulation/math.cpp'),
                    str(vendor / 'src/assets/rom.cpp'), '-o', str(exe)]
            built = subprocess.run(args, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            env = os.environ.copy()
            if mingw.exists():
                env['PATH'] = str(mingw) + os.pathsep + env.get('PATH', '')
            run = subprocess.run([str(exe)], env=env, capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

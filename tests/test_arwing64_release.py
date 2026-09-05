"""Exercise the real packager with synthetic staging inputs, never ROM assets."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile

ROOT=Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which('powershell'), 'PowerShell packaging test')
class ReleaseGateTests(unittest.TestCase):
    def package(self, root, extra=None):
        for name in ['tools','docs','build/assets','runtime']:
            (root/name).mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT/'tools/make_release.ps1',root/'tools/make_release.ps1')
        (root/'README.md').write_text('synthetic packaging fixture')
        (root/'docs/ARWING64.md').write_text('synthetic documentation')
        (root/'config.ini').write_text('[Features]\nArwing64 = 1\nArwing64Rom = C:/private/owner.z64\n')
        (root/'build/StarFoxSNESRecomp.exe').write_bytes(b'synthetic-test-version')
        (root/'build/assets/launcher.txt').write_text('launcher fixture')
        for name in ['SDL3.dll','libgcc_s_seh-1.dll','libstdc++-6.dll','libwinpthread-1.dll']:
            (root/'runtime'/name).write_bytes(b'synthetic dll fixture')
        if extra:
            path=root/'build/assets'/extra[0]
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(extra[1])
        return subprocess.run(['powershell','-NoProfile','-File',str(root/'tools/make_release.ps1'),
                               '-Version','synthetic-test-version','-BuildDir','build',
                               '-RuntimeBinDir',str(root/'runtime')],capture_output=True,text=True)

    def test_clean_archive_has_no_owner_path(self):
        with tempfile.TemporaryDirectory(prefix='arwing_release_') as directory:
            root=Path(directory); result=self.package(root)
            self.assertEqual(result.returncode,0,result.stderr)
            archive=next((root/'release-stage').glob('*.zip'))
            with zipfile.ZipFile(archive) as z:
                config=z.read('config.ini').decode()
                self.assertIn('Arwing64 = 0',config)
                self.assertNotIn('owner.z64',config)
                self.assertIn('docs/ARWING64.md',z.namelist())
                self.assertFalse(any('\\' in n for n in z.namelist()))

    def test_nested_caches_and_renamed_mesh_are_rejected(self):
        for path,data in [('arwing64_cache/audio/local.txt',b'fixture'),
                          ('sounds/sfx_laser.wav',b'fixture'),
                          ('private/game.z64',b'fixture'),
                          ('models/renamed.data',b'N64MESHBfixture')]:
            with self.subTest(path=path), tempfile.TemporaryDirectory(prefix='arwing_release_') as directory:
                root=Path(directory); result=self.package(root,(path,data))
                self.assertNotEqual(result.returncode,0)
                self.assertIn('release stage',result.stderr)
                self.assertFalse(list((root/'release-stage').glob('*.zip')))

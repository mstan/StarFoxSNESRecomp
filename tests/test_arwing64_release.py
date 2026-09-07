"""Exercise the real packager with synthetic staging inputs, never ROM assets."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from check_release_payload import check_payload


class AppImageReleaseGateTests(unittest.TestCase):
    def test_payload_checks_nested_owner_assets_and_renamed_mesh(self):
        for path,data in [('assets/gallery.png',b'synthetic screenshot'),
                          ('assets/arwing64_cache/audio/local.txt',b'fixture'),
                          ('mods/sfx_laser.wav',b'fixture'),
                          ('assets/game.z64',b'fixture'),
                          ('assets/renamed.data',b'N64MESHBfixture')]:
            with self.subTest(path=path), tempfile.TemporaryDirectory() as directory:
                root=Path(directory)
                file=root/path
                file.parent.mkdir(parents=True)
                file.write_bytes(data)
                if path.endswith('gallery.png'):
                    self.assertEqual(check_payload(root),1)
                else:
                    with self.assertRaisesRegex(ValueError,'release stage'):
                        check_payload(root)


@unittest.skipUnless(shutil.which('powershell'), 'PowerShell packaging test')
class ReleaseGateTests(unittest.TestCase):
    def package(self, root, extra=None):
        for name in ['tools','docs/images','build/assets','runtime']:
            (root/name).mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT/'tools/make_release.ps1',root/'tools/make_release.ps1')
        (root/'README.md').write_text('synthetic packaging fixture')
        (root/'docs/ARWING64.md').write_text('synthetic documentation')
        (root/'docs/TRUE_WIDESCREEN.md').write_text('synthetic widescreen documentation')
        (root/'docs/images/gallery.png').write_bytes(b'synthetic screenshot fixture')
        (root/'config.ini').write_text('[Graphics]\nWidescreen = 21:9\nEnhancedRenderer = 1\n[Features]\nArwing64 = 1\nArwing64Rom = C:/private/owner.z64\n')
        (root/'build/StarFoxSNESRecomp.exe').write_bytes(b'synthetic-test-version')
        (root/'build/keybinds.ini').write_text('developer bindings must not ship')
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
                self.assertIn('EnhancedRenderer = 0',config)
                self.assertIn('Widescreen = 0',config)
                self.assertNotIn('owner.z64',config)
                self.assertIn('docs/ARWING64.md',z.namelist())
                self.assertIn('docs/TRUE_WIDESCREEN.md',z.namelist())
                self.assertIn('docs/images/gallery.png',z.namelist())
                self.assertNotIn('keybinds.ini',z.namelist())
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

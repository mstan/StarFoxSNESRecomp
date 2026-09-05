"""Owner-ROM audio checks. No copyrighted inputs or oracle outputs are tracked.

SF64_ROM, SF64_DECOMP, ARWING64_TOOL enable the native/reference differential.
The oracle compiles the decomp's actual my_decodeframe, not a Python rewrite.
"""
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import wave

ROM = os.environ.get('SF64_ROM')
DECOMP = os.environ.get('SF64_DECOMP')
TOOL = os.environ.get('ARWING64_TOOL')
CC = os.environ.get('CC', 'C:/msys64/mingw64/bin/cc.exe')
ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(Path(CC).exists(), 'native C compiler required')
class AudioRuntimeTests(unittest.TestCase):
    def test_protocol_and_mixer(self):
        with tempfile.TemporaryDirectory(prefix='arwing64_runtime_') as directory:
            root = Path(directory)
            audio = root/'audio'; audio.mkdir()
            names = ['laser','twin_laser','beam_laser','bomb_shot','bomb_explode','boost',
                     'brake','wing_hit','wing_lost','body_hit','explosion','shield_deflect','engine_loop','roll']
            for name in names:
                path = audio/f'sfx_{name}.wav'
                with wave.open(str(path), 'wb') as wav:
                    wav.setparams((2,2,32040,1000,'NONE','not compressed'))
                    wav.writeframes(struct.pack('<hh',1000,-1000)*1000)
                if name == 'engine_loop':
                    data = bytearray(path.read_bytes())
                    smpl = bytearray(68); smpl[:4] = b'smpl'
                    struct.pack_into('<I',smpl,4,60)
                    struct.pack_into('<I',smpl,36,1)
                    struct.pack_into('<II',smpl,52,100,999)
                    data.extend(smpl); struct.pack_into('<I',data,4,len(data)-8)
                    path.write_bytes(data)
            exe = root/'test.exe'
            subprocess.run([CC,'-std=c11','-Wall','-Wextra','-Werror',
                            '-I'+str(ROOT/'src/mods/arwing64'),'-I'+str(ROOT/'snesrecomp/runner/src'),
                            str(ROOT/'tests/arwing64_audio_runtime.c'),
                            str(ROOT/'src/mods/arwing64/arwing64_audio.c'),
                            str(ROOT/'snesrecomp/runner/src/mod_audio.c'),'-o',str(exe)],check=True,capture_output=True)
            subprocess.run([str(exe),str(root)],check=True,capture_output=True)


@unittest.skipUnless(ROM and DECOMP and TOOL, 'owner ROM, decomp and native tool required')
class AudioDifferentialTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='arwing64_audio_')
        cls.root = Path(cls.temp.name)
        cls.rom = Path(ROM).read_bytes()
        cls.font = cls.rom[0x119710:0x119710+0x2f00]
        oracle = (Path(DECOMP)/'tools/aifc_decode.c').as_posix()
        source = cls.root/'oracle.c'
        source.write_text('''#define main unused_decoder_main
#include "''' + oracle + '''"
#undef main
int main(int argc, char **argv) {
  FILE *f = fopen(argv[1], "rb"), *out = fopen(argv[2], "wb");
  s16 order, predictors; s32 ***table, state[16] = {0}; u8 frame[9];
  if (!f || !out) return 2;
  readaifccodebook(f, &table, &order, &predictors);
  while (fread(frame, 1, 9, f) == 9) {
    my_decodeframe(frame, state, order, table);
    for (int i=0; i<16; i++) {
      u16 v = (u16)clamp_to_s16(state[i]);
      fputc(v & 255, out); fputc(v >> 8, out);
    }
  }
  fclose(f); return fclose(out) != 0;
}
''', encoding='utf-8')
        cls.oracle = cls.root/'oracle.exe'
        subprocess.run([CC, '-O2', str(source), '-lm', '-o', str(cls.oracle)], check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_vadpcm_matches_decomp_decoder(self):
        b = self.font
        for instrument in [0,1,2,3,4,7,8,9,12,17,18,20,21,22,80,88,97]:
            with self.subTest(instrument=instrument):
                ip = struct.unpack_from('>I', b, 4+instrument*4)[0]
                sp = struct.unpack_from('>I', b, ip+16)[0]
                _, addr, loop, book = struct.unpack_from('>IIII', b, sp)
                end = struct.unpack_from('>I', b, loop+4)[0]
                order, predictors = struct.unpack_from('>II', b, book)
                frames = (end+15)//16
                data = struct.pack('>HH', order, predictors)+b[book+8:book+8+order*predictors*16]
                data += self.rom[0x137730+addr:0x137730+addr+frames*9]
                source, expected = self.root/'sample.adpcm', self.root/'reference.pcm'
                source.write_bytes(data)
                subprocess.run([str(self.oracle), str(source), str(expected)], check=True, capture_output=True)
                subprocess.run([TOOL, ROM, str(self.root), '--sample', str(instrument)], check=True, capture_output=True)
                actual = (self.root/'sample.pcm').read_bytes()
                self.assertEqual(len(actual), end*2)
                self.assertEqual(actual, expected.read_bytes()[:end*2])

    def test_rendered_cues_manifest_and_loop(self):
        subprocess.run([TOOL, ROM, str(self.root), '--audio'], check=True, capture_output=True)
        audio = self.root/'audio'
        manifest = (audio/'manifest.sha256').read_text().splitlines()
        self.assertEqual(len(manifest), 15)
        first = {}
        for line in manifest[1:]:
            digest, name = line.split()
            data = (audio/name).read_bytes()
            first[name] = data
            self.assertEqual(hashlib.sha256(data).hexdigest(), digest)
            with wave.open(str(audio/name), 'rb') as wav:
                self.assertEqual((wav.getnchannels(), wav.getsampwidth(), wav.getframerate()), (2, 2, 32040))
                self.assertGreater(wav.getnframes(), 1000)
                self.assertLessEqual(wav.getnframes(), 32040*8)
                pcm = struct.unpack('<'+'h'*(wav.getnframes()*2), wav.readframes(wav.getnframes()))
                self.assertGreater(max(pcm)-min(pcm), 500)
                self.assertLess(max(abs(v) for v in pcm), 32767)
            if name == 'sfx_engine_loop.wav':
                p = data.index(b'smpl')
                self.assertEqual(struct.unpack_from('<I', data, p+36)[0], 1)
                start, end = struct.unpack_from('<II', data, p+52)
                self.assertGreater(start, 0)
                self.assertLess(start, end)
        subprocess.run([TOOL, ROM, str(self.root), '--audio'], check=True, capture_output=True)
        for name, data in first.items():
            self.assertEqual(data, (audio/name).read_bytes(), name)
        verify = [TOOL,ROM,str(self.root),'--verify-audio']
        self.assertEqual(subprocess.run(verify,capture_output=True).returncode,0)
        laser=audio/'sfx_laser.wav'
        damaged=bytearray(laser.read_bytes()); damaged[-1]^=1; laser.write_bytes(damaged)
        self.assertNotEqual(subprocess.run(verify,capture_output=True).returncode,0)
        laser.write_bytes(first['sfx_laser.wav'])
        self.assertEqual(subprocess.run(verify,capture_output=True).returncode,0)


if __name__ == '__main__':
    unittest.main()

"""Arwing64 extractor checks.

Two layers:

* Static checks that always run: the seam document records the byte-verified
  retail addresses the mod depends on, and the config surface exposes the
  Arwing64 keys.
* A differential check that runs only when an owner ROM and the decomp's
  Torch output are available (never in CI without them):

      ARWING64_TOOL=build-arwing/arwing64_tool.exe
      SF64_ROM=<path to baserom.us.rev1.z64>
      SF64_DECOMP=<path to sonicdcer/sf64 checkout after `make assets`>

  It runs the tool, then compares the per-display-list triangle counts of the
  blob against the gsSP1Triangle/gsSP2Triangles census of the decomp's
  src/assets/ast_arwing/ast_arwing.c and ast_common/ast_common.c.
"""
import json
import os
import re
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

ARWING_LISTS = [
    "aAwBodyDL", "aAwRightWingDL", "aAwLeftWingDL", "aAwRightWingBrokenDL",
    "aAwLeftWingBrokenDL", "aAwFlap1DL", "aAwFlap2DL", "aAwFlap3DL",
    "aAwFlap4DL", "aAwLaserGun1DL",
]
# The decomp names the second gun list without the "DL" suffix.
DECOMP_ALIASES = {"aAwLaserGun2DL": "aAwLaserGun2L"}
COMMON_LISTS = {"aOrbDL_red": "aOrbDL", "aOrbDL_blue": "aOrbDL",
                "aOrbDL_green": "aOrbDL", "aOrbDL_orange": "aOrbDL",
                "aBarrelRollDL": "aBarrelRollDL",
                "aLaserShotGreenDL": "aLaserShotGreenDL",
                "aLaserShotBlueDL": "aLaserShotBlueDL"}


def read_text(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def count_triangles(c_source, symbol):
    match = re.search(r"^Gfx " + re.escape(symbol) + r"\[\] = \{(.*?)^\};",
                      c_source, re.S | re.M)
    if not match:
        return None
    body = match.group(1)
    return (2 * len(re.findall(r"gsSP2Triangles\(", body)) +
            len(re.findall(r"gsSP1Triangle\(", body)))


class SeamDocumentTests(unittest.TestCase):
    def test_seam_doc_records_retail_addresses(self):
        doc = read_text(os.path.join(ROOT, "docs", "ARWING64_SOURCE_SEAMS.md"))
        for needle in ["$06:80D5", "0x300D5", "$00:D2CC", "$14D6", "$14D7",
                       "$14DB", "$70:2B26", "$70:01BC", "$1501", "$2143",
                       "$03:B7F9", "$35", "$32", "$33"]:
            self.assertIn(needle, doc, needle)

    def test_read_only_retail_guard_matches_seam_doc(self):
        src = read_text(os.path.join(ROOT, "src", "mods", "arwing64", "arwing64.c"))
        self.assertIn("kRomShapeTable = 0x300d5", src)
        self.assertIn("0x20, 0xd3, 0xac, 0xd3, 0x74, 0xd3", src)
        self.assertNotIn("guarded_patch_apply", src)

    def test_config_surface(self):
        cfg = read_text(os.path.join(ROOT, "src", "config.c"))
        for key in ["Arwing64", "Arwing64Rom", "Arwing64Supersample", "Arwing64Sfx"]:
            self.assertIn('"%s"' % key, cfg)
        hdr = read_text(os.path.join(ROOT, "src", "config.h"))
        self.assertIn("arwing64_rom_path[1024]", hdr)


@unittest.skipUnless(os.environ.get("ARWING64_TOOL") and os.environ.get("SF64_ROM")
                     and os.environ.get("SF64_DECOMP"),
                     "ARWING64_TOOL, SF64_ROM and SF64_DECOMP not set")
class DifferentialTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.out_dir = tempfile.mkdtemp(prefix="arwing64_")
        subprocess.run([os.environ["ARWING64_TOOL"], os.environ["SF64_ROM"],
                        cls.out_dir, "--json"], check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        with open(os.path.join(cls.out_dir, "stats.json"), encoding="utf-8") as f:
            cls.stats = json.load(f)
        decomp = os.environ["SF64_DECOMP"]
        cls.arwing_c = read_text(os.path.join(decomp, "src", "assets",
                                              "ast_arwing", "ast_arwing.c"))
        cls.common_c = read_text(os.path.join(decomp, "src", "assets",
                                              "ast_common", "ast_common.c"))

    def test_arwing_list_triangle_counts(self):
        for name in ARWING_LISTS + list(DECOMP_ALIASES):
            expected = count_triangles(self.arwing_c, DECOMP_ALIASES.get(name, name))
            self.assertIsNotNone(expected, name)
            self.assertEqual(self.stats["lists"][name], expected, name)

    def test_common_effect_lists(self):
        for name, symbol in COMMON_LISTS.items():
            expected = count_triangles(self.common_c, symbol)
            self.assertIsNotNone(expected, symbol)
            self.assertEqual(self.stats["lists"][name], expected, name)

    def test_skeleton_and_poses(self):
        self.assertEqual(self.stats["limbs"], 18)
        self.assertEqual(self.stats["poses"], 3)
        self.assertGreaterEqual(self.stats["textures"], 15)
        self.assertTrue(os.path.getsize(os.path.join(self.out_dir, "arwing64.bin")) > 100000)

    def test_open_wings_are_visible_and_wider_than_closed(self):
        # A triangle census cannot catch fully transparent wings. Check the
        # rasterised silhouette of the original poses, with both wings intact.
        widths = []
        for pose in ("wings_closed", "wings_open"):
            with tempfile.TemporaryDirectory(prefix="arwing64_pose_") as out:
                run = subprocess.run(
                    [os.environ["ARWING64_TOOL"], os.environ["SF64_ROM"],
                     out, "--pose", pose, "--break", "none"], check=True,
                    capture_output=True, text=True)
                rear = re.search(
                    r"preview [^\n]*preview_rear.png: pixels=(\d+).*"
                    r"bbox=(-?\d+),(-?\d+)\.\.(-?\d+),(-?\d+)", run.stdout)
                self.assertIsNotNone(rear, run.stdout)
                self.assertGreater(int(rear[1]), 3000)
                widths.append(int(rear[4]) - int(rear[2]))
        self.assertGreater(widths[1], widths[0] * 1.5,
                           "expanded wing geometry must change the visible silhouette")


if __name__ == "__main__":
    unittest.main()

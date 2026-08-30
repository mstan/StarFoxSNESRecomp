import tempfile
import unittest
from pathlib import Path

import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "snesrecomp" / "recompiler"))

import ingest_starfox_enhanced_symbols as ingest
import starfox_enhanced_compare as compare
import starfox_enhanced_symbols as symbols
from v2.cfg_loader import load_bank_cfg


class StarFoxEnhancedSymbolsTests(unittest.TestCase):
    def test_parse_common_symbol_formats(self):
        text = """
        $01:AC1D RenderObjects
        LoadAudio = $03B109
        $83:8123 HighMirror
        03C000 BankFirst
        IgnoredLowRam = $001234
        """

        parsed = symbols.parse_symbols_text(text)
        labels = {(entry.name, entry.bank, entry.addr) for entry in parsed}

        self.assertIn(("RenderObjects", 0x01, 0xAC1D), labels)
        self.assertIn(("LoadAudio", 0x03, 0xB109), labels)
        self.assertIn(("HighMirror", 0x03, 0x8123), labels)
        self.assertIn(("BankFirst", 0x03, 0xC000), labels)
        self.assertIn(("IgnoredLowRam", 0x00, 0x1234), labels)
        self.assertEqual(
            "constant_or_direct",
            next(entry.space for entry in parsed if entry.name == "IgnoredLowRam"),
        )

    def test_compare_reports_matching_address_name(self):
        symbol_entries = symbols.parse_symbols_text("LoadAudio = $03B109\n")
        cfg_funcs = [
            compare.CfgFunc(
                name="LoadAudio",
                bank=0x03,
                addr=0xB109,
                source="recomp/bank03.cfg",
                source_line=6,
            )
        ]

        result = compare.compare(symbol_entries, cfg_funcs)

        self.assertEqual(1, len(result["matched"]))
        self.assertEqual([], result["name_mismatches"])
        self.assertEqual([], result["missing_cfg"])
        self.assertEqual([], result["cfg_only"])

    def test_filter_symbols_can_select_rom_only(self):
        parsed = symbols.parse_symbols_text(
            "DoThing = $03B109\n"
            "RamThing = $7E81EF\n"
            "ConstantThing = $000003\n"
        )

        rom_only = symbols.filter_symbols(parsed, {"rom"})

        self.assertEqual(["DoThing"], [entry.name for entry in rom_only])

    def test_load_cfg_funcs_reads_bank_context(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            recomp = root / "recomp"
            recomp.mkdir()
            (recomp / "bank03.cfg").write_text(
                "bank = 3\nfunc LoadAudio b109 end:b269\n",
                encoding="utf-8",
            )

            funcs = compare.load_cfg_funcs(root)

        self.assertEqual(1, len(funcs))
        self.assertEqual(("LoadAudio", 0x03, 0xB109), (funcs[0].name, funcs[0].bank, funcs[0].addr))

    def test_symbol_directive_does_not_promote_to_func(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            cfg = Path(temp_dir) / "bank03.cfg"
            cfg.write_text("bank = 03\nsymbol 03c000 NamedData\n", encoding="utf-8")

            loaded = load_bank_cfg(str(cfg))

        self.assertEqual([], loaded.entries)
        self.assertEqual([(0x03C000, "NamedData")], [(s.addr_24, s.name) for s in loaded.symbols])

    def test_ingest_writes_cfg_overlay_and_preserves_hand_declarations(self):
        symbol_entries = symbols.parse_symbols_text(
            "LoadAudio = $03B109\n"
            "NewThing = $03C000\n"
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            recomp = Path(temp_dir) / "recomp"
            recomp.mkdir()
            cfg = recomp / "bank03.cfg"
            cfg.write_text(
                "bank = 03\nfunc LoadAudio b109 end:b269\n",
                encoding="utf-8",
            )

            summary = ingest.emit_per_bank(
                symbols.filter_symbols(symbol_entries, {"rom"}),
                recomp,
                Path("SYMBOLS.TXT"),
            )
            content = cfg.read_text(encoding="utf-8")

        self.assertEqual({"banks": 1, "symbols": 1, "suppressed": 1}, summary)
        self.assertIn("func LoadAudio b109 end:b269", content)
        self.assertNotIn("symbol 03b109 LoadAudio", content)
        self.assertIn("symbol 03c000 NewThing", content)
        self.assertIn(ingest.INGEST_BEGIN, content)


if __name__ == "__main__":
    unittest.main()

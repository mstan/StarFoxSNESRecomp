#!/usr/bin/env python3
"""Compare Star Fox Enhanced symbols with local recomp function declarations."""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

SCRIPT_PATH = Path(str(__file__).replace("\\", "/")).resolve()

sys.path.insert(0, str(SCRIPT_PATH.parent))
import starfox_enhanced_symbols as enhanced_symbols  # noqa: E402


BANK_RE = re.compile(r"^\s*bank\s*=\s*(?P<bank>\d+)\b")
FUNC_RE = re.compile(
    r"^\s*func\s+(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s+(?P<addr>[0-9A-Fa-f]{1,4})\b"
)


@dataclass(frozen=True)
class CfgFunc:
    name: str
    bank: int
    addr: int
    source: str
    source_line: int

    @property
    def key(self) -> tuple[int, int]:
        return (self.bank, self.addr)


def parse_cfg_file(path: Path) -> list[CfgFunc]:
    funcs: list[CfgFunc] = []
    bank: int | None = None
    for line_no, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw_line.split("#", 1)[0].strip()
        if not line:
            continue
        bank_match = BANK_RE.match(line)
        if bank_match:
            bank = int(bank_match.group("bank"))
            continue
        func_match = FUNC_RE.match(line)
        if func_match and bank is not None:
            funcs.append(
                CfgFunc(
                    name=func_match.group("name"),
                    bank=bank & 0x7F,
                    addr=int(func_match.group("addr"), 16),
                    source=str(path),
                    source_line=line_no,
                )
            )
    return funcs


def load_cfg_funcs(root: Path) -> list[CfgFunc]:
    funcs: list[CfgFunc] = []
    for path in sorted((root / "recomp").glob("*.cfg")):
        funcs.extend(parse_cfg_file(path))
    return sorted(funcs, key=lambda func: (func.bank, func.addr, func.name))


def compare(symbols: list[enhanced_symbols.SymbolEntry], cfg_funcs: list[CfgFunc]) -> dict[str, object]:
    cfg_by_addr: dict[tuple[int, int], list[CfgFunc]] = {}
    symbol_by_addr: dict[tuple[int, int], list[enhanced_symbols.SymbolEntry]] = {}

    for func in cfg_funcs:
        cfg_by_addr.setdefault(func.key, []).append(func)
    for symbol in symbols:
        symbol_by_addr.setdefault(symbol.key, []).append(symbol)

    matched = []
    name_mismatches = []
    missing_cfg = []
    cfg_only = []

    for key, entries in sorted(symbol_by_addr.items()):
        funcs = cfg_by_addr.get(key)
        if funcs is None:
            missing_cfg.extend(entries)
            continue
        for symbol in entries:
            if any(func.name == enhanced_symbols.c_identifier(symbol.name) for func in funcs):
                matched.append({"symbol": symbol, "cfg": funcs})
            else:
                name_mismatches.append({"symbol": symbol, "cfg": funcs})

    for key, funcs in sorted(cfg_by_addr.items()):
        if key not in symbol_by_addr:
            cfg_only.extend(funcs)

    return {
        "matched": matched,
        "name_mismatches": name_mismatches,
        "missing_cfg": missing_cfg,
        "cfg_only": cfg_only,
    }


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=SCRIPT_PATH.parents[1])
    parser.add_argument("--symbols", type=Path, help="Path to a SYMBOLS.TXT file.")
    parser.add_argument(
        "--experience",
        choices=["original", "ex", "all"],
        default="original",
        help="Default symbol search target when --symbols is omitted.",
    )
    parser.add_argument("--json", action="store_true", help="Emit machine-readable comparison.")
    parser.add_argument("--max-items", type=int, default=40, help="Maximum rows per human-readable bucket.")
    parser.add_argument(
        "--allow-missing",
        action="store_true",
        help="Return success when no generated upstream symbol file is present.",
    )
    return parser


def entry_label(entry: enhanced_symbols.SymbolEntry) -> str:
    return f"${entry.bank:02X}:{entry.addr:04X} {entry.name}"


def cfg_label(func: CfgFunc) -> str:
    return f"${func.bank:02X}:{func.addr:04X} {func.name} ({func.source}:{func.source_line})"


def main(argv: list[str] | None = None) -> int:
    args = build_arg_parser().parse_args(argv)
    root = args.root.resolve()
    symbol_path = enhanced_symbols.choose_symbol_path(root, args.symbols, args.experience)

    if symbol_path is None or not symbol_path.exists():
        if args.allow_missing:
            print("No Star Fox Enhanced / UltraStarFox SYMBOLS.TXT found; comparison skipped.")
            return 0
        return enhanced_symbols.main(
            [
                "--root",
                str(root),
                "--experience",
                args.experience,
            ]
        )

    symbols = enhanced_symbols.parse_symbols_text(
        symbol_path.read_text(encoding="utf-8", errors="replace")
    )
    symbols = enhanced_symbols.filter_symbols(symbols, {"rom"})
    cfg_funcs = load_cfg_funcs(root)
    result = compare(symbols, cfg_funcs)

    if args.json:
        print(
            json.dumps(
                {
                    "symbol_path": str(symbol_path),
                    "summary": {
                        "symbols": len(symbols),
                        "cfg_funcs": len(cfg_funcs),
                        "matched": len(result["matched"]),
                        "name_mismatches": len(result["name_mismatches"]),
                        "missing_cfg": len(result["missing_cfg"]),
                        "cfg_only": len(result["cfg_only"]),
                    },
                    "name_mismatches": [
                        {
                            "symbol": asdict(item["symbol"]),
                            "cfg": [asdict(func) for func in item["cfg"]],
                        }
                        for item in result["name_mismatches"]
                    ],
                    "missing_cfg": [asdict(entry) for entry in result["missing_cfg"]],
                    "cfg_only": [asdict(func) for func in result["cfg_only"]],
                },
                indent=2,
                sort_keys=True,
            )
        )
        return 0

    print(f"Symbol file: {symbol_path}")
    print(f"Enhanced symbols: {len(symbols)}")
    print(f"Local cfg funcs: {len(cfg_funcs)}")
    print(f"Address/name matches: {len(result['matched'])}")
    print(f"Same address, different name: {len(result['name_mismatches'])}")
    print(f"Enhanced symbols without cfg func: {len(result['missing_cfg'])}")
    print(f"Cfg funcs without Enhanced symbol: {len(result['cfg_only'])}")

    if result["name_mismatches"]:
        print()
        print("Same address, different name:")
        for item in result["name_mismatches"][: args.max_items]:
            cfg_names = ", ".join(func.name for func in item["cfg"])
            print(f"  {entry_label(item['symbol'])} -> {cfg_names}")

    if result["missing_cfg"]:
        print()
        print("Enhanced symbols without cfg func:")
        for entry in result["missing_cfg"][: args.max_items]:
            print(f"  {entry_label(entry)}")

    if result["cfg_only"]:
        print()
        print("Cfg funcs without Enhanced symbol:")
        for func in result["cfg_only"][: args.max_items]:
            print(f"  {cfg_label(func)}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Inventory Star Fox Enhanced / UltraStarFox symbol files.

This is deliberately read-only. It parses symbol names and LoROM addresses from
the upstream-generated SYMBOLS.TXT file so developers can review candidate names
before changing recomp metadata.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable


SCRIPT_PATH = Path(str(__file__).replace("\\", "/")).resolve()

NAME_RE = r"[A-Za-z_.$@?][A-Za-z0-9_.$@?]*"
HEX_RE = r"(?:\$|0x)?[0-9A-Fa-f]+"

PATTERNS = [
    re.compile(
        rf"^\s*(?:\$|0x)?(?P<bank>[0-9A-Fa-f]{{2}})[:/](?P<addr>[0-9A-Fa-f]{{4}})\s+(?P<name>{NAME_RE})\b"
    ),
    re.compile(
        rf"^\s*(?P<name>{NAME_RE})\s*(?:=|equ|EQU)?\s*(?P<pc>{HEX_RE})\b"
    ),
    re.compile(
        rf"^\s*(?P<pc>{HEX_RE})\s+(?P<name>{NAME_RE})\b"
    ),
]


@dataclass(frozen=True)
class SymbolEntry:
    name: str
    bank: int
    addr: int
    pc: int
    space: str
    source_line: int

    @property
    def key(self) -> tuple[int, int]:
        return (self.bank, self.addr)


def parse_hex(value: str) -> int:
    value = value.strip()
    if value.startswith("$"):
        value = value[1:]
    return int(value, 16)


def classify_address(bank: int, addr: int) -> str:
    if bank in {0x7E, 0x7F}:
        return "wram"
    if 0x70 <= bank <= 0x75:
        return "superfx_ram"
    if addr >= 0x8000 and bank < 0x70:
        return "rom"
    return "constant_or_direct"


def split_pc(pc: int) -> tuple[int, int, str]:
    bank = (pc >> 16) & 0xFF
    addr = pc & 0xFFFF
    normalized_bank = bank & 0x7F if bank < 0x80 else bank
    return (normalized_bank, addr, classify_address(bank, addr))


def parse_symbols_text(text: str) -> list[SymbolEntry]:
    entries: list[SymbolEntry] = []
    seen: set[tuple[int, int, str]] = set()

    for line_no, raw_line in enumerate(text.splitlines(), 1):
        line = raw_line.split(";", 1)[0].split("#", 1)[0].strip()
        if not line:
            continue

        parsed: SymbolEntry | None = None
        for pattern in PATTERNS:
            match = pattern.match(line)
            if not match:
                continue

            name = match.group("name")
            if "bank" in match.groupdict() and match.group("bank") is not None:
                bank = parse_hex(match.group("bank")) & 0x7F
                addr = parse_hex(match.group("addr"))
                pc = (bank << 16) | addr
                space = classify_address(bank, addr)
            else:
                pc = parse_hex(match.group("pc"))
                bank, addr, space = split_pc(pc)

            parsed = SymbolEntry(
                name=name,
                bank=bank,
                addr=addr,
                pc=pc,
                space=space,
                source_line=line_no,
            )
            break

        if parsed is None:
            continue

        key = (parsed.bank, parsed.addr, parsed.name)
        if key in seen:
            continue
        seen.add(key)
        entries.append(parsed)

    return sorted(entries, key=lambda entry: (entry.bank, entry.addr, entry.name))


def filter_symbols(entries: Iterable[SymbolEntry], spaces: set[str]) -> list[SymbolEntry]:
    if "all" in spaces:
        return list(entries)
    return [entry for entry in entries if entry.space in spaces]


def default_symbol_paths(root: Path, experience: str) -> list[Path]:
    enhanced = root / "third_party" / "starfox-enhanced"
    paths = []
    if experience in {"original", "all"}:
        paths.append(enhanced / "upstream-ultrastarfox" / "SYMBOLS.TXT")
    if experience in {"ex", "all"}:
        paths.append(enhanced / "upstream-star-fox-ex" / "SYMBOLS.TXT")
    paths.append(enhanced / "SYMBOLS.TXT")
    return paths


def choose_symbol_path(root: Path, explicit: Path | None, experience: str) -> Path | None:
    if explicit is not None:
        return explicit
    for path in default_symbol_paths(root, experience):
        if path.exists():
            return path
    return None


def load_reference_metadata(root: Path) -> dict[str, object]:
    enhanced = root / "third_party" / "starfox-enhanced"
    metadata: dict[str, object] = {
        "starfox_enhanced_path": str(enhanced),
    }

    if enhanced.exists():
        try:
            commit = subprocess.check_output(
                ["git", "-C", str(enhanced), "rev-parse", "HEAD"],
                text=True,
                stderr=subprocess.DEVNULL,
            ).strip()
            metadata["starfox_enhanced_commit"] = commit
        except (OSError, subprocess.CalledProcessError):
            pass

    for name in ["upstream.json", "upstream-ex.json"]:
        path = enhanced / "config" / name
        if not path.exists():
            continue
        try:
            metadata[name] = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            metadata[name] = {"error": str(exc)}

    return metadata


def summarize(entries: Iterable[SymbolEntry]) -> dict[str, object]:
    by_bank: dict[str, int] = {}
    by_space: dict[str, int] = {}
    total = 0
    for entry in entries:
        by_bank[f"{entry.bank:02X}"] = by_bank.get(f"{entry.bank:02X}", 0) + 1
        by_space[entry.space] = by_space.get(entry.space, 0) + 1
        total += 1
    return {
        "total": total,
        "by_bank": dict(sorted(by_bank.items())),
        "by_space": dict(sorted(by_space.items())),
    }


def c_identifier(name: str) -> str:
    ident = re.sub(r"[^A-Za-z0-9_]", "_", name)
    ident = re.sub(r"_+", "_", ident).strip("_")
    if not ident:
        ident = "unnamed"
    if ident[0].isdigit():
        ident = f"sf_{ident}"
    return ident


def print_cfg_snippets(entries: Iterable[SymbolEntry]) -> None:
    current_bank: int | None = None
    for entry in entries:
        if current_bank != entry.bank:
            current_bank = entry.bank
            print()
            print(f"# bank {current_bank:02X}")
        print(f"func {c_identifier(entry.name)} {entry.addr:04x}  # {entry.name}")


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
    parser.add_argument("--json", action="store_true", help="Emit machine-readable inventory.")
    parser.add_argument(
        "--space",
        action="append",
        choices=["all", "rom", "wram", "superfx_ram", "constant_or_direct"],
        default=[],
        help="Restrict inventory output. May be repeated; default is all.",
    )
    parser.add_argument(
        "--cfg-snippets",
        action="store_true",
        help="Print reviewed-by-hand recomp cfg snippets to stdout.",
    )
    parser.add_argument(
        "--allow-missing",
        action="store_true",
        help="Return success when no generated upstream symbol file is present.",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_arg_parser().parse_args(argv)
    root = args.root.resolve()
    symbol_path = choose_symbol_path(root, args.symbols, args.experience)

    if symbol_path is None or not symbol_path.exists():
        searched = [str(path) for path in default_symbol_paths(root, args.experience)]
        payload = {
            "status": "missing",
            "searched": searched,
            "hint": "Build or copy the upstream SYMBOLS.TXT declared by third_party/starfox-enhanced.",
        }
        if args.json:
            print(json.dumps(payload, indent=2, sort_keys=True))
        else:
            print("No Star Fox Enhanced / UltraStarFox SYMBOLS.TXT found.")
            print("Searched:")
            for path in searched:
                print(f"  {path}")
            print("Generate it with the reference submodule's upstream build flow, then rerun this tool.")
        return 0 if args.allow_missing else 1

    text = symbol_path.read_text(encoding="utf-8", errors="replace")
    parsed_entries = parse_symbols_text(text)
    entries = filter_symbols(parsed_entries, set(args.space or ["all"]))
    metadata = load_reference_metadata(root)
    summary = summarize(entries)

    if args.json:
        print(
            json.dumps(
                {
                    "status": "ok",
                    "symbol_path": str(symbol_path),
                    "metadata": metadata,
                    "summary": summary,
                    "symbols": [asdict(entry) for entry in entries],
                },
                indent=2,
                sort_keys=True,
            )
        )
    else:
        print(f"Symbol file: {symbol_path}")
        print(f"Symbols parsed: {summary['total']} of {len(parsed_entries)}")
        for space, count in summary["by_space"].items():
            print(f"  {space}: {count}")
        for bank, count in summary["by_bank"].items():
            print(f"  bank {bank}: {count}")

    if args.cfg_snippets:
        print_cfg_snippets(entries)

    return 0 if entries else 1


if __name__ == "__main__":
    raise SystemExit(main())

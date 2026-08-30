#!/usr/bin/env python3
"""Overlay Star Fox Enhanced / UltraStarFox symbols onto recomp cfg files.

This writes non-promoting `symbol <pc24> <name>` cfg directives, not `func`
roots. Star Fox's SYMBOLS.TXT contains CPU labels, Super FX routines, shape
data, tables, text, constants, and RAM variables; treating every ROM symbol as
65816 code would create bogus recomp functions. The `symbol` directive gives
the v2 name resolver an authoritative label when real analysis reaches an
address, while leaving reachability/function discovery to the recompiler.

Idempotent: each cfg file's auto-ingested section is delimited and replaced
wholesale on every run. Hand-authored `func`, `name`, and `symbol` lines
outside the auto section win by address.
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path
from typing import Iterable

SCRIPT_PATH = Path(str(__file__).replace("\\", "/")).resolve()
sys.path.insert(0, str(SCRIPT_PATH.parent))

import starfox_enhanced_symbols as enhanced_symbols  # noqa: E402


INGEST_BEGIN = (
    "# >>> AUTO-INGESTED FROM Star Fox Enhanced SYMBOLS.TXT "
    "- do not hand-edit between markers >>>"
)
INGEST_END = "# <<< END AUTO-INGESTED <<<"

FUNC_DECL_RE = re.compile(
    r"^\s*func\s+\S+\s+([0-9A-Fa-f]{1,6})\b"
)
LABEL_DECL_RE = re.compile(
    r"^\s*(?:name|symbol)\s+([0-9A-Fa-f]{1,6})\s+\S+\b"
)


def _read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def _write_text(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8", newline="\n")


def _declared_pcs_without_auto_section(existing: str, bank: int) -> tuple[str, set[int]]:
    section_re = re.compile(
        re.escape(INGEST_BEGIN) + r".*?" + re.escape(INGEST_END) + r"\n?",
        flags=re.DOTALL,
    )
    hand_block = section_re.sub("", existing)
    declared: set[int] = set()
    for line in hand_block.splitlines():
        match = FUNC_DECL_RE.match(line) or LABEL_DECL_RE.match(line)
        if not match:
            continue
        try:
            raw_pc = int(match.group(1), 16) & 0xFFFFFF
            declared.add(raw_pc if raw_pc > 0xFFFF else ((bank << 16) | raw_pc))
        except ValueError:
            pass
    return hand_block, declared


def _entries_by_bank(
    entries: Iterable[enhanced_symbols.SymbolEntry],
) -> dict[int, list[enhanced_symbols.SymbolEntry]]:
    by_bank: dict[int, list[enhanced_symbols.SymbolEntry]] = defaultdict(list)
    seen: set[tuple[int, int, str]] = set()
    for entry in entries:
        if entry.space != "rom":
            continue
        key = (entry.bank, entry.addr, enhanced_symbols.c_identifier(entry.name))
        if key in seen:
            continue
        seen.add(key)
        by_bank[entry.bank].append(entry)
    for bank in by_bank:
        by_bank[bank].sort(key=lambda item: (item.addr, item.source_line, item.name))
    return by_bank


def emit_per_bank(
    entries: list[enhanced_symbols.SymbolEntry],
    output_dir: Path,
    source_path: Path,
    dry_run: bool = False,
) -> dict[str, int]:
    by_bank = _entries_by_bank(entries)
    total_written = 0
    total_suppressed = 0

    for bank in sorted(by_bank):
        cfg_path = output_dir / f"bank{bank:02x}.cfg"
        existing = _read_text(cfg_path) if cfg_path.exists() else ""
        hand_block, hand_pcs = _declared_pcs_without_auto_section(existing, bank)

        filtered = [
            entry for entry in by_bank[bank]
            if ((entry.bank << 16) | entry.addr) not in hand_pcs
        ]
        total_written += len(filtered)
        total_suppressed += len(by_bank[bank]) - len(filtered)

        section_lines = [
            INGEST_BEGIN,
            f"# Source: {source_path.as_posix()}",
            "# Regenerate via: py tools/ingest_starfox_enhanced_symbols.py",
            "# These are non-promoting labels; do not convert this block to `name`.",
            f"# {len(filtered)} entries "
            f"({len(by_bank[bank]) - len(filtered)} suppressed by hand-authored declarations).",
        ]
        for entry in filtered:
            ident = enhanced_symbols.c_identifier(entry.name)
            comment = f"  # {entry.name}" if ident != entry.name else ""
            section_lines.append(
                f"symbol {entry.bank:02x}{entry.addr:04x} {ident}{comment}"
            )
        section_lines.append(INGEST_END)
        new_section = "\n".join(section_lines) + "\n"

        if existing:
            new_content = hand_block.rstrip() + "\n\n" + new_section
        else:
            new_content = (
                f"# bank{bank:02x}.cfg - auto-created by "
                "ingest_starfox_enhanced_symbols.py\n\n"
                f"bank = {bank:02x}\n\n"
                f"{new_section}"
            )

        if dry_run:
            print(
                f"[dry-run] {cfg_path}: {len(filtered)} symbols "
                f"({len(by_bank[bank]) - len(filtered)} suppressed)"
            )
        else:
            _write_text(cfg_path, new_content)
            print(
                f"wrote {cfg_path}: {len(filtered)} symbols "
                f"({len(by_bank[bank]) - len(filtered)} suppressed)"
            )

    return {
        "banks": len(by_bank),
        "symbols": total_written,
        "suppressed": total_suppressed,
    }


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=SCRIPT_PATH.parents[1])
    parser.add_argument("--symbols", type=Path, help="Path to SYMBOLS.TXT.")
    parser.add_argument("--output", type=Path, help="Path to recomp cfg dir.")
    parser.add_argument(
        "--experience",
        choices=["original", "ex", "all"],
        default="original",
        help="Default symbol search target when --symbols is omitted.",
    )
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--allow-missing", action="store_true")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_arg_parser().parse_args(argv)
    root = args.root.resolve()
    output_dir = (args.output or (root / "recomp")).resolve()
    symbol_path = enhanced_symbols.choose_symbol_path(
        root, args.symbols, args.experience
    )

    if symbol_path is None or not symbol_path.exists():
        print("No Star Fox Enhanced / UltraStarFox SYMBOLS.TXT found.", file=sys.stderr)
        for path in enhanced_symbols.default_symbol_paths(root, args.experience):
            print(f"  searched: {path}", file=sys.stderr)
        return 0 if args.allow_missing else 1
    if not output_dir.is_dir():
        print(f"--output not a directory: {output_dir}", file=sys.stderr)
        return 1

    entries = enhanced_symbols.filter_symbols(
        enhanced_symbols.parse_symbols_text(_read_text(symbol_path)), {"rom"}
    )
    source_path = symbol_path.resolve()
    try:
        source_path = source_path.relative_to(root)
    except ValueError:
        pass

    summary = emit_per_bank(entries, output_dir, source_path, args.dry_run)
    print(
        f"ingested {summary['symbols']} ROM symbols across {summary['banks']} banks "
        f"({summary['suppressed']} suppressed by hand-authored declarations)",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

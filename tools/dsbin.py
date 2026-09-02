"""Shared helpers for MarkerPatch binary analysis.

Used by the sibling scripts in this directory to parse the repo's signature
table, map file offsets to virtual addresses, and compare candidate function
sites between Dead Space 1 and Dead Space 2.

Game paths default to a typical install and can be overridden with the
MARKERPATCH_DS1 / MARKERPATCH_DS2 environment variables, or per-script flags.
"""

import bisect
import csv
import difflib
import os
import pathlib
import re

import capstone
import pefile

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent

DEFAULT_DS1 = r"D:\GOG\Dead Space\Dead Space.exe"
DEFAULT_DS2 = r"D:\SteamLibrary\steamapps\common\Dead Space 2\deadspace2.exe"


def ds1_path(override=None):
    return override or os.environ.get("MARKERPATCH_DS1", DEFAULT_DS1)


def ds2_path(override=None):
    return override or os.environ.get("MARKERPATCH_DS2", DEFAULT_DS2)


def add_common_args(parser):
    parser.add_argument("--ds1", help="path to Dead Space.exe")
    parser.add_argument("--ds2", help="path to deadspace2.exe")
    return parser


# ---------------------------------------------------------------- signatures

# Matches: ScanModuleSignature(g_State.GameModule, "55 8B EC ...", "PatchName")
SIG_CALL = re.compile(
    r'ScanModuleSignature\(\s*[^,]+,\s*"([0-9A-Fa-f?\s]+)"\s*,\s*"([^"]+)"', re.S
)


def load_signatures(repo_root=REPO_ROOT):
    """Return every signature declared in src/, as a list of dicts."""
    rows = []
    for path in sorted(pathlib.Path(repo_root).glob("src/**/*.cpp")):
        text = path.read_text(encoding="utf-8-sig", errors="replace")
        for pattern, name in SIG_CALL.findall(text):
            rows.append(
                {
                    "feature": path.stem,
                    "path": path.relative_to(repo_root).as_posix(),
                    "name": name,
                    "pattern": pattern.strip(),
                }
            )
    return rows


def pattern_to_regex(pattern):
    """Convert an IDA-style byte pattern ('55 8B ?? EC') to a bytes regex."""
    parts = []
    for token in pattern.split():
        if token.startswith("?"):
            parts.append(b".")
        else:
            parts.append(re.escape(bytes([int(token, 16)])))
    return re.compile(b"".join(parts), re.DOTALL)


def pattern_length(pattern):
    return len(pattern.split())


# --------------------------------------------------------------------- image


class Image:
    """A PE image loaded for offset/VA translation and pattern searching."""

    def __init__(self, path):
        self.path = str(path)
        self.pe = pefile.PE(self.path, fast_load=True)
        self.raw = open(self.path, "rb").read()
        self.base = self.pe.OPTIONAL_HEADER.ImageBase

    def va(self, offset):
        """File offset -> virtual address, or None if outside any section."""
        for section in self.pe.sections:
            start = section.PointerToRawData
            if start <= offset < start + section.SizeOfRawData:
                return self.base + section.VirtualAddress + (offset - start)
        return None

    def offset(self, va):
        """Virtual address -> file offset, or None if outside any section."""
        rva = va - self.base
        for section in self.pe.sections:
            start = section.VirtualAddress
            if start <= rva < start + section.SizeOfRawData:
                return section.PointerToRawData + (rva - start)
        return None

    def find(self, pattern):
        """All file offsets matching a byte pattern."""
        return [m.start() for m in pattern_to_regex(pattern).finditer(self.raw)]


# ----------------------------------------------------------------- disasm

_MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
_HEX = re.compile(r"0x[0-9a-f]+")


def _mask_absolutes(op_str):
    """Blank out absolute addresses so only structure and small offsets compare.

    Call targets and globals differ between the two binaries even when the code
    is identical, so they must not count against similarity. Values below 0x1000
    are kept: those are struct offsets and immediates, which are exactly what we
    DO want to compare.
    """
    return _HEX.sub(
        lambda m: m.group(0) if int(m.group(0), 16) < 0x1000 else "ABS", op_str
    )


def normalized_disasm(raw, va, count=40):
    """Disassemble up to `count` instructions into normalized text lines."""
    out = []
    for insn in _MD.disasm(raw, va or 0):
        out.append(f"{insn.mnemonic} {_mask_absolutes(insn.op_str)}")
        if len(out) >= count:
            break
    return out


def disasm_lines(raw, va, count=40, stop_at_ret=False):
    """Disassemble for display, preserving real addresses."""
    out = []
    for insn in _MD.disasm(raw, va or 0):
        out.append(f"{insn.address:08X}  {insn.mnemonic:<7} {insn.op_str}")
        if len(out) >= count:
            break
        if stop_at_ret and insn.mnemonic in ("ret", "jmp"):
            break
    return out


def similarity(img_a, off_a, img_b, off_b, count=40, window=256):
    """Structural similarity (0.0-1.0) of the code at two sites."""
    a = normalized_disasm(img_a.raw[off_a : off_a + window], img_a.va(off_a), count)
    b = normalized_disasm(img_b.raw[off_b : off_b + window], img_b.va(off_b), count)
    return difflib.SequenceMatcher(None, a, b).ratio()


CONFIRMED, LIKELY, FALSE_POSITIVE, NO_MATCH = (
    "confirmed",
    "likely",
    "false positive",
    "no match",
)


def verdict(score):
    """Classify a similarity score.

    The thresholds are deliberately conservative. Two of the first ten
    cross-matches found were collisions on a generic MSVC prologue that happened
    to share a stack frame size, so a raw pattern hit is never sufficient.
    """
    if score >= 0.85:
        return CONFIRMED
    if score >= 0.55:
        return LIKELY
    return FALSE_POSITIVE


# ------------------------------------------------------- function inventory


def load_function_inventory(csv_path):
    """Load a Ghidra ExportNeighborhood function CSV, sorted by address."""
    funcs = []
    with open(csv_path, newline="") as fh:
        for row in csv.DictReader(fh):
            funcs.append((int(row["address"], 16), int(row["size"]), row["name"]))
    funcs.sort()
    return funcs


def containing_function(funcs, va):
    """Which function in the inventory contains `va`? -> (start, size, name)."""
    starts = [f[0] for f in funcs]
    index = bisect.bisect_right(starts, va) - 1
    if index < 0:
        return None
    start, size, name = funcs[index]
    return (start, size, name) if va < start + size else None

"""Resolve a string to the function that references it, by immediate-value xref.

Havok emits profiler markers and assert strings from inside the very functions
we want to find, so locating the code that pushes a string's address is often
the cheapest way to name a function. This finds the string, computes its VA,
then searches for that VA as a 4-byte little-endian immediate.

Requires a function inventory CSV from the ExportNeighborhood Ghidra script.

Usage:
    python tools/xref.py --csv export/Dead_Space_exe_functions.csv \
        StBuildJacobians MiNumJacobians "Constraint Solver"
"""

import argparse
import re
import struct

import dsbin

# Havok markers that proved useful for locating the DS1 constraint solver.
DEFAULT_TARGETS = [
    "StBuildJacobians",
    "MiNumJacobians",
    "StSolverExport",
    "Constraint Solver",
    "hkpBallSocketChainData",
    "buildJacobianFunc",
    "solverTau",
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("strings", nargs="*", default=None, help="strings to resolve")
    parser.add_argument(
        "--exe", help="binary to search (defaults to DS1)"
    )
    parser.add_argument(
        "--csv", required=True, help="function inventory CSV from ExportNeighborhood"
    )
    parser.add_argument(
        "--max-hits", type=int, default=3, help="xrefs to show per string"
    )
    args = parser.parse_args()

    img = dsbin.Image(args.exe or dsbin.ds1_path())
    funcs = dsbin.load_function_inventory(args.csv)
    targets = args.strings or DEFAULT_TARGETS

    print(f"{'STRING':<26}{'STR VA':<12}{'XREF VA':<12}{'IN FUNCTION':<22}{'SIZE':>7}")
    print("-" * 82)
    for target in targets:
        offset = img.raw.find(target.encode() + b"\x00")
        if offset < 0:
            print(f"{target:<26}(string not found)")
            continue
        string_va = img.va(offset)
        immediate = struct.pack("<I", string_va)
        hits = [m.start() for m in re.finditer(re.escape(immediate), img.raw)]
        if not hits:
            print(f"{target:<26}0x{string_va:08X}  (no immediate xref)")
            continue

        shown = 0
        for hit in hits:
            hit_va = img.va(hit)
            if hit_va is None:
                continue
            found = dsbin.containing_function(funcs, hit_va)
            if not found:
                continue
            start, size, name = found
            label = target if shown == 0 else ""
            print(
                f"{label:<26}0x{string_va:08X}  0x{hit_va:08X}  {name:<22}{size:>7}"
            )
            shown += 1
            if shown >= args.max_hits:
                break
        if shown == 0:
            print(
                f"{target:<26}0x{string_va:08X}  "
                f"({len(hits)} xrefs, none inside a known function -- likely data/reflection)"
            )


if __name__ == "__main__":
    main()

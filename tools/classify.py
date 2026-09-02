"""Verify and classify DS1/DS2 signature cross-matches.

sigscan.py finds byte-identical hits, but a raw hit is not proof: short
patterns beginning with a generic MSVC prologue (55 8B EC 83 E4 F0 ...) collide
with unrelated functions that happen to share a stack frame size. This script
disassembles both sites, masks absolute addresses, and scores the structural
similarity so collisions can be thrown out.

Usage:
    python tools/classify.py                      # verdict table
    python tools/classify.py --disasm             # side-by-side disassembly
    python tools/classify.py --disasm --only InitializePhysicsSolverParameters
"""

import argparse

import dsbin


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    dsbin.add_common_args(parser)
    parser.add_argument(
        "--disasm", action="store_true", help="print side-by-side disassembly"
    )
    parser.add_argument(
        "--only", action="append", help="limit to these signature names (repeatable)"
    )
    parser.add_argument(
        "--count", type=int, default=40, help="instructions to compare (default 40)"
    )
    args = parser.parse_args()

    ds2 = dsbin.Image(dsbin.ds2_path(args.ds2))
    ds1 = dsbin.Image(dsbin.ds1_path(args.ds1))

    results = []
    for sig in dsbin.load_signatures():
        if args.only and sig["name"] not in args.only:
            continue
        hits2 = ds2.find(sig["pattern"])
        hits1 = ds1.find(sig["pattern"])
        if not hits2 or not hits1:
            continue
        # A pattern can hit several times in DS1; keep the best-scoring site.
        best = max(
            hits1, key=lambda off: dsbin.similarity(ds2, hits2[0], ds1, off, args.count)
        )
        score = dsbin.similarity(ds2, hits2[0], ds1, best, args.count)
        results.append((sig, hits2[0], best, score))

    if args.disasm:
        for sig, off2, off1, score in sorted(results, key=lambda r: -r[3]):
            print("=" * 78)
            print(f"{sig['name']}   similarity {score * 100:.0f}%  ({dsbin.verdict(score)})")
            print("=" * 78)
            for label, img, off in (("DS2", ds2, off2), ("DS1", ds1, off1)):
                print(f"  [{label}] VA 0x{img.va(off):08X}")
                for line in dsbin.disasm_lines(
                    img.raw[off : off + 256], img.va(off), args.count, stop_at_ret=True
                ):
                    print(f"        {line}")
                print()
        return

    width = max((len(s["name"]) for s, _, _, _ in results), default=20) + 2
    print(f"{'SIGNATURE':<{width}}{'SIM':>5}  {'DS2 VA':<12}{'DS1 VA':<12}{'VERDICT':<16}SOURCE")
    print("-" * (width + 62))
    tally = {}
    for sig, off2, off1, score in sorted(results, key=lambda r: -r[3]):
        v = dsbin.verdict(score)
        tally[v] = tally.get(v, 0) + 1
        print(
            f"{sig['name']:<{width}}{score * 100:>4.0f}%  "
            f"0x{ds2.va(off2):08X}  0x{ds1.va(off1):08X}  {v:<16}{sig['feature']}"
        )
    print("\n=== TALLY ===")
    for v in (dsbin.CONFIRMED, dsbin.LIKELY, dsbin.FALSE_POSITIVE):
        print(f"  {v:<15} {tally.get(v, 0)}")


if __name__ == "__main__":
    main()

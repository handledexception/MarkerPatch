"""Generate the per-feature signature port matrix used by PLAN.md.

Emits a summary table to stdout and the full per-signature detail to
sigtable.json for further processing.

Usage:
    python tools/table.py [--json sigtable.json]
"""

import argparse
import collections
import json

import dsbin


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    dsbin.add_common_args(parser)
    parser.add_argument("--json", default="sigtable.json", help="detail output path")
    args = parser.parse_args()

    ds2 = dsbin.Image(dsbin.ds2_path(args.ds2))
    ds1 = dsbin.Image(dsbin.ds1_path(args.ds1))

    rows = dsbin.load_signatures()
    for row in rows:
        hits2 = ds2.find(row["pattern"])
        hits1 = ds1.find(row["pattern"])
        row["ds2_va"] = f"0x{ds2.va(hits2[0]):08X}" if hits2 else None
        row["ds2_hits"] = len(hits2)

        if not hits1 or not hits2:
            row["ds1_va"], row["similarity"], row["verdict"] = None, None, dsbin.NO_MATCH
            continue

        best = max(hits1, key=lambda off: dsbin.similarity(ds2, hits2[0], ds1, off))
        score = dsbin.similarity(ds2, hits2[0], ds1, best)
        row["ds1_va"] = f"0x{ds1.va(best):08X}"
        row["similarity"] = round(score * 100)
        row["verdict"] = dsbin.verdict(score)

    with open(args.json, "w") as fh:
        json.dump(rows, fh, indent=1)

    by_feature = collections.OrderedDict()
    for row in rows:
        by_feature.setdefault(row["feature"], []).append(row)

    print(f"{'FEATURE':<32}{'#SIG':>5}{'CONF':>6}{'LIKE':>6}{'FP':>4}{'NONE':>6}")
    print("-" * 62)
    for feature, entries in sorted(by_feature.items()):
        counts = collections.Counter(e["verdict"] for e in entries)
        print(
            f"{feature:<32}{len(entries):>5}{counts[dsbin.CONFIRMED]:>6}"
            f"{counts[dsbin.LIKELY]:>6}{counts[dsbin.FALSE_POSITIVE]:>4}"
            f"{counts[dsbin.NO_MATCH]:>6}"
        )
    print(f"\ntotal signatures: {len(rows)}   features: {len(by_feature)}")
    print(f"detail -> {args.json}")


if __name__ == "__main__":
    main()

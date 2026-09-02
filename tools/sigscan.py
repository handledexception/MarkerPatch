"""Scan every signature declared in src/ against both game binaries on disk.

This is a fast first pass: it only finds byte-identical code, so a "no match"
is NOT evidence that a function is absent from the other game -- only that its
codegen differs. Use BSim for structural matching, and classify.py to verify
anything this reports as a cross-match.

Usage:
    python tools/sigscan.py [--ds1 PATH] [--ds2 PATH]
"""

import argparse

import dsbin


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    dsbin.add_common_args(parser)
    args = parser.parse_args()

    sigs = dsbin.load_signatures()
    images = {}
    for label, path in (("DS1", dsbin.ds1_path(args.ds1)), ("DS2", dsbin.ds2_path(args.ds2))):
        try:
            images[label] = dsbin.Image(path)
            print(f"loaded {label}: {path}")
        except OSError as exc:
            print(f"SKIP {label}: {exc}")
    print(f"{len(sigs)} signatures parsed from src/\n")

    rows = []
    for sig in sigs:
        hits = {label: img.find(sig["pattern"]) for label, img in images.items()}
        rows.append((sig, len(hits.get("DS2", [])), len(hits.get("DS1", []))))

    width = max(len(s["name"]) for s, _, _ in rows) + 2
    print(f"{'SIGNATURE':<{width}}{'LEN':>4}{'DS2':>6}{'DS1':>6}   SOURCE")
    print("-" * (width + 56))
    for sig, ds2, ds1 in sorted(rows, key=lambda r: (-r[2], -r[1], r[0]["name"])):
        flag = "  <== ALSO IN DS1" if ds1 else ""
        print(
            f"{sig['name']:<{width}}{dsbin.pattern_length(sig['pattern']):>4}"
            f"{ds2:>6}{ds1:>6}   {sig['path']}{flag}"
        )

    both = sum(1 for _, d2, d1 in rows if d2 and d1)
    only2 = sum(1 for _, d2, d1 in rows if d2 and not d1)
    only1 = sum(1 for _, d2, d1 in rows if d1 and not d2)
    neither = sum(1 for _, d2, d1 in rows if not d1 and not d2)
    print("\n=== SUMMARY ===")
    print(f"  found in BOTH binaries : {both}")
    print(f"  found in DS2 only      : {only2}")
    print(f"  found in DS1 only      : {only1}")
    print(f"  found in NEITHER       : {neither}")
    print("\nVerify every cross-match with: python tools/classify.py")


if __name__ == "__main__":
    main()

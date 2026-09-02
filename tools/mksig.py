"""Generate a MarkerPatch byte signature for a function address.

MarkerPatch scans byte patterns rather than hardcoded addresses, because
addresses shift between game builds (GOG / Steam / retail). Reverse engineering
gives us an address; this turns it into a pattern that survives rebasing and
minor build differences.

Bytes that encode an absolute address or a relative branch target are wildcarded,
since those differ between builds even when the instruction does not. The pattern
is extended one instruction at a time until it is unique across the image.

Usage:
    python tools/mksig.py 0x008EA540
    python tools/mksig.py 0x007D0360 --exe "D:\\GOG\\Dead Space\\Dead Space.exe"
    python tools/mksig.py --batch sigs.txt      # lines of: <VA> <Name>
"""

import argparse

import capstone

import dsbin

MAX_PATTERN_BYTES = 128  # MarkerPatch's scanner caps patterns at 256


def _wildcard_ranges(insn, image_lo, image_hi):
    """Byte ranges within an instruction that should become wildcards."""
    ranges = []
    enc = getattr(insn, "encoding", None)
    if enc is None:
        return ranges

    # Relative branch targets (call/jmp/jcc rel32) always shift between builds.
    is_branch = insn.group(capstone.CS_GRP_CALL) or insn.group(capstone.CS_GRP_JUMP)

    if enc.imm_size and (is_branch or _looks_like_address(insn, enc, image_lo, image_hi, "imm")):
        ranges.append((enc.imm_offset, enc.imm_size))

    # A displacement pointing into the image is an absolute global reference.
    if enc.disp_size and _looks_like_address(insn, enc, image_lo, image_hi, "disp"):
        ranges.append((enc.disp_offset, enc.disp_size))

    return ranges


def _looks_like_address(insn, enc, lo, hi, kind):
    """Does this operand's value fall inside the loaded image?"""
    size = enc.imm_size if kind == "imm" else enc.disp_size
    offset = enc.imm_offset if kind == "imm" else enc.disp_offset
    if size != 4:
        return False
    raw = insn.bytes[offset : offset + 4]
    if len(raw) != 4:
        return False
    value = int.from_bytes(raw, "little", signed=False)
    return lo <= value < hi


def make_signature(image, va, max_bytes=MAX_PATTERN_BYTES, min_bytes=0, verbose=False):
    """Build the shortest unique pattern starting at `va`. Returns (pattern, n_insns).

    `min_bytes` keeps extending past the first point of uniqueness. A pattern that
    is unique in this build can still be fragile across other DS1 releases, so a
    little extra context is cheap insurance.
    """
    offset = image.offset(va)
    if offset is None:
        raise ValueError(f"0x{va:08X} is not inside any section of {image.path}")

    lo = image.base
    hi = image.base + image.pe.OPTIONAL_HEADER.SizeOfImage

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True

    tokens = []
    count = 0
    for insn in md.disasm(image.raw[offset : offset + max_bytes * 2], va):
        if len(tokens) >= max_bytes:
            break

        masked = set()
        for start, size in _wildcard_ranges(insn, lo, hi):
            masked.update(range(start, start + size))

        for i, byte in enumerate(insn.bytes):
            tokens.append("??" if i in masked else f"{byte:02X}")
        count += 1

        if verbose:
            print(f"    +{len(tokens):>3}  {insn.mnemonic:<7} {insn.op_str}")

        pattern = " ".join(tokens)
        hits = image.find(pattern)
        if len(hits) == 1 and hits[0] == offset and len(tokens) >= min_bytes:
            # Never end on a wildcard: trailing '??' adds length but no specificity.
            while tokens and tokens[-1] == "??":
                tokens.pop()
            return " ".join(tokens), count

    pattern = " ".join(tokens)
    hits = image.find(pattern)
    raise ValueError(
        f"could not make 0x{va:08X} unique within {max_bytes} bytes "
        f"({len(hits)} matches with the longest pattern tried)"
    )


def _emit(image, va, name, verbose, min_bytes=0):
    try:
        pattern, insns = make_signature(image, va, min_bytes=min_bytes, verbose=verbose)
    except ValueError as exc:
        print(f"FAILED  {name or hex(va)}: {exc}")
        return False

    hits = image.find(pattern)
    ok = len(hits) == 1 and image.va(hits[0]) == va
    label = name or f"sub_{va:08X}"
    print(f'  "{pattern}"')
    print(
        f"      -> {label}  VA 0x{va:08X}  "
        f"{len(pattern.split())} bytes / {insns} insns  "
        f"{'UNIQUE' if ok else 'NOT UNIQUE (!!)'}"
    )
    return ok


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("address", nargs="?", help="virtual address, e.g. 0x008EA540")
    parser.add_argument("--name", help="symbolic name for the emitted line")
    parser.add_argument("--exe", help="binary (defaults to DS1)")
    parser.add_argument(
        "--batch", help="file of '<VA> <Name>' lines to process in bulk"
    )
    parser.add_argument(
        "--min-bytes",
        type=int,
        default=12,
        help="keep extending past uniqueness to at least this many bytes (default 12)",
    )
    parser.add_argument("-v", "--verbose", action="store_true", help="show instructions")
    args = parser.parse_args()

    image = dsbin.Image(args.exe or dsbin.ds1_path())
    print(f"image: {image.path}\n")

    if args.batch:
        total = good = 0
        with open(args.batch) as fh:
            for line in fh:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                parts = line.split(None, 1)
                va = int(parts[0], 16)
                name = parts[1] if len(parts) > 1 else None
                total += 1
                good += _emit(image, va, name, args.verbose, args.min_bytes)
                print()
        print(f"{good}/{total} signatures generated and verified unique")
        return

    if not args.address:
        parser.error("give an address or --batch")
    _emit(image, int(args.address, 16), args.name, args.verbose, args.min_bytes)


if __name__ == "__main__":
    main()

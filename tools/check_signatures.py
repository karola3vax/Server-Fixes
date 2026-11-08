"""Checks every signature in src/signatures.h against a libserver.so: each must match exactly once in its code.

usage: python check_signatures.py <path to game/csgo/bin/linuxsteamrt64/libserver.so>
Exit code 0 when all match once. Read-only; needs only Python 3.
"""
import pathlib
import re
import struct
import sys

SIGNATURES = pathlib.Path(__file__).resolve().parent.parent / "src" / "signatures.h"


def signatures():
    text = SIGNATURES.read_text(encoding="utf-8")
    return re.findall(r'constexpr const char \*(k(?:Sig|Fn)\w+) = "([^"]+)";', text)


def code_segments(data):
    """(file offset, size) of every executable PT_LOAD segment of a 64-bit little-endian ELF."""
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        sys.exit("not a 64-bit little-endian ELF file")
    phoff, = struct.unpack_from("<Q", data, 0x20)
    phentsize, phnum = struct.unpack_from("<HH", data, 0x36)
    segments = []
    for i in range(phnum):
        p_type, p_flags, p_offset, _vaddr, _paddr, p_filesz = struct.unpack_from("<IIQQQQ", data, phoff + i * phentsize)
        if p_type == 1 and p_flags & 1:  # PT_LOAD, PF_X
            segments.append((p_offset, p_filesz))
    return segments


def to_regex(signature):
    parts = [b"." if token in ("?", "??") else re.escape(bytes([int(token, 16)])) for token in signature.split()]
    return re.compile(b"".join(parts), re.DOTALL)


def behind_hook_jump(signature):
    """Mirrors sf::BehindHookJump: the first 6 bytes as wildcards, or None with fewer than 8 concrete bytes left."""
    tokens = signature.split()
    if len(tokens) <= 6:
        return None
    rest = tokens[6:]
    if sum(token not in ("?", "??") for token in rest) < 8:
        return None
    return " ".join(["?"] * 6 + rest)


def count(data, segments, signature):
    regex = to_regex(signature)
    return sum(sum(1 for _ in regex.finditer(data, offset, offset + size)) for offset, size in segments)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    data = pathlib.Path(sys.argv[1]).read_bytes()
    segments = code_segments(data)
    failed = 0
    for name, signature in signatures():
        hits = count(data, segments, signature)
        # The plugin falls back to this when another hook's jump replaced the first bytes; it must be unique too.
        hooked = behind_hook_jump(signature) if name.startswith("kFn") else None
        hooked_hits = count(data, segments, hooked) if hooked else None
        ok = hits == 1 and hooked_hits in (None, 1)
        failed += not ok
        fallback = "" if hooked_hits is None else f", behind a hook jump {hooked_hits}"
        print(f"{'ok' if ok else 'FAIL':4} {name}: {hits} match{'es' if hits != 1 else ''}{fallback}")
    print(f"{len(signatures()) - failed} of {len(signatures())} signatures match once")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

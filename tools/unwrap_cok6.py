"""Unwraps decrypted COK6 files into plain SWF and PNG files.

A COK6 file (the decrypted `.cok6`/`.png` pak entries) is a 128-byte header
followed by the payload:

    0x00  "6KOC" magic (COK6 stored little-endian), then 12 zero bytes
    0x10  u32 payload size
    0x14  u32 payload offset (0x80)
    0x18  padding (0xCC) up to 0x80

The payload is a standard uncompressed SWF ("FWS", mostly version 6) or a PNG.
The SWF's own length field can be smaller than the payload; the wrapper size
is the real one. SWFs contain a non-standard tag 148 (a bitmap).
Decrypted entries also end with an 8-byte trailer after the payload.

    python tools/extract/unwrap_cok6.py --pak <decrypted> --out <folder>
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


def unwrap(data: bytes) -> tuple[str, bytes]:
    if data[:4] != b"6KOC":
        raise ValueError("not a COK6 file")
    size, offset = struct.unpack_from("<II", data, 0x10)
    payload = data[offset:offset + size]
    if payload[:3] in (b"FWS", b"CWS"):
        # The SWF header's length is often stale (tags were appended after it
        # was written), so trust the wrapper's size and fix the header so
        # standard SWF tools read the whole file.
        swf = bytearray(payload)
        struct.pack_into("<I", swf, 4, len(swf))
        return "swf", bytes(swf)
    if payload[:4] == b"\x89PNG":
        return "png", payload
    return "bin", payload


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--pak", type=Path, default=root / "extracted" / "pak")
    parser.add_argument("--out", type=Path, default=root / "extracted" / "swf")
    args = parser.parse_args()

    counts: dict[str, int] = {}
    for source in sorted(args.pak.rglob("*")):
        if not source.is_file():
            continue
        data = source.read_bytes()
        if data[:4] != b"6KOC":
            continue
        kind, payload = unwrap(data)
        target = args.out / source.parent.name / (source.stem + "." + kind)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
        counts[kind] = counts.get(kind, 0) + 1
    print(f"unwrapped {counts} to {args.out}")


if __name__ == "__main__":
    main()

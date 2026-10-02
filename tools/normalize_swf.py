"""Rewrites Castle Crashers' ActionScript into standard SWF bytecode.

The Behemoth's build tools emit fused actions that other SWF tools don't know. In
the game each one's handler also performs the one-byte action that follows it
and skips it:

- 0xA0-0xA4: a Push (ordinary Push data: one value for A0/A2, two for
  A1/A3/A4) fused with the following GetVariable, GetMember, SetMember or
  DefineLocal. Rewritten to Push (0x96), the following action then does the
  rest.
- 0x70: ToBoolean fused with the following Not, so the pair leaves the
  boolean value. Rewritten to Not (0x12), `Not; Not` gives the same result.

Both replacements keep sizes, but the rewriter still recomputes branch offsets
(If, Jump), function body sizes (DefineFunction, DefineFunction2), With and Try
block sizes, and rebuilds tag, sprite and SWF lengths, so actions can safely be
removed or resized later. Scripts in DoAction, DoInitAction and
PlaceObject2 clip actions (onClipEvent) are rewritten.

    python tools/extract/normalize_swf.py --swf <unwrapped> --out <folder>
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

PUSH_VARIANTS = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4}
NOT_VARIANTS = {0x70}
DROP: set[int] = set()


class Action:
    __slots__ = ("offset", "op", "body")

    def __init__(self, offset: int, op: int, body: bytes):
        self.offset = offset
        self.op = op
        self.body = body

    def size(self) -> int:
        return 1 + (2 + len(self.body) if self.op >= 0x80 else 0)


def parse_actions(code: bytes) -> list[Action]:
    actions = []
    pos = 0
    while pos < len(code):
        offset = pos
        op = code[pos]
        pos += 1
        body = b""
        if op >= 0x80:
            length = struct.unpack_from("<H", code, pos)[0]
            pos += 2
            body = code[pos:pos + length]
            pos += length
        actions.append(Action(offset, op, body))
        if op == 0 and pos >= len(code):
            break
    return actions


def rewrite_actions(code: bytes) -> bytes:
    actions = parse_actions(code)
    end_offset = len(code)

    # Old offset of each action -> old offset of the next one.
    kept = [a for a in actions if a.op not in DROP]
    for a in kept:
        if a.op in PUSH_VARIANTS:
            a.op = 0x96
        elif a.op in NOT_VARIANTS:
            a.op = 0x12

    # Map old offsets to new offsets (dropped actions map to the next kept one).
    new_offset = {}
    position = 0
    for a in actions:
        new_offset[a.offset] = position
        if a.op not in DROP:
            position += a.size()
    new_offset[end_offset] = position

    def remap(old: int) -> int:
        # Targets always land on action boundaries in well-formed code.
        return new_offset[old]

    out = bytearray()
    for a in kept:
        old_next = a.offset + (1 + (2 + len(a.body) if a.op >= 0x80 else 0))
        body = bytearray(a.body)
        if a.op in (0x99, 0x9D):  # Jump, If: signed offset from the next action
            target = old_next + struct.unpack_from("<h", body, 0)[0]
            struct.pack_into("<h", body, 0, remap(target) - remap(old_next))
        elif a.op == 0x9B:  # DefineFunction: name, params, then u16 code size
            size_pos = len(body) - 2
            struct.pack_into("<H", body, size_pos,
                             remap(old_next + struct.unpack_from("<H", body, size_pos)[0]) - remap(old_next))
        elif a.op == 0x8E:  # DefineFunction2: code size is the last u16
            size_pos = len(body) - 2
            struct.pack_into("<H", body, size_pos,
                             remap(old_next + struct.unpack_from("<H", body, size_pos)[0]) - remap(old_next))
        elif a.op == 0x94:  # With: u16 block size
            struct.pack_into("<H", body, 0, remap(old_next + struct.unpack_from("<H", body, 0)[0]) - remap(old_next))
        elif a.op == 0x8F:  # Try: flags, try/catch/finally sizes
            try_size, catch_size, finally_size = struct.unpack_from("<HHH", body, 1)
            try_end = old_next + try_size
            catch_end = try_end + catch_size
            finally_end = catch_end + finally_size
            struct.pack_into("<HHH", body, 1,
                             remap(try_end) - remap(old_next),
                             remap(catch_end) - remap(try_end),
                             remap(finally_end) - remap(catch_end))
        out.append(a.op)
        if a.op >= 0x80:
            out += struct.pack("<H", len(body))
            out += body
    return bytes(out)


class BitReader:
    def __init__(self, data: bytes, pos: int):
        self.data = data
        self.bit = pos * 8

    def read(self, n: int) -> int:
        value = 0
        for _ in range(n):
            byte = self.data[self.bit >> 3]
            value = (value << 1) | ((byte >> (7 - (self.bit & 7))) & 1)
            self.bit += 1
        return value

    def byte_pos(self) -> int:
        return (self.bit + 7) >> 3


def skip_matrix(data: bytes, pos: int) -> int:
    r = BitReader(data, pos)
    if r.read(1):
        n = r.read(5)
        r.read(n * 2)
    if r.read(1):
        n = r.read(5)
        r.read(n * 2)
    n = r.read(5)
    r.read(n * 2)
    return r.byte_pos()


def skip_cxform_alpha(data: bytes, pos: int) -> int:
    r = BitReader(data, pos)
    has_add = r.read(1)
    has_mult = r.read(1)
    n = r.read(4)
    if has_mult:
        r.read(n * 4)
    if has_add:
        r.read(n * 4)
    return r.byte_pos()


def rewrite_place_object2(body: bytes, version: int) -> bytes:
    flags = body[0]
    if not flags & 0x80:
        return body
    pos = 3
    if flags & 0x02:
        pos += 2
    if flags & 0x04:
        pos = skip_matrix(body, pos)
    if flags & 0x08:
        pos = skip_cxform_alpha(body, pos)
    if flags & 0x10:
        pos += 2
    if flags & 0x20:
        pos = body.index(0, pos) + 1
    if flags & 0x40:
        pos += 2
    flag_size = 4 if version >= 6 else 2
    out = bytearray(body[:pos + 2 + flag_size])  # reserved + all event flags
    pos += 2 + flag_size
    while True:
        event = body[pos:pos + flag_size]
        pos += flag_size
        if int.from_bytes(event, "little") == 0:
            out += event
            break
        size = struct.unpack_from("<I", body, pos)[0]
        pos += 4
        record = body[pos:pos + size]
        pos += size
        prefix = b""
        if flag_size == 4 and int.from_bytes(event, "little") & 0x00020000:  # ClipEventKeyPress
            prefix, record = record[:1], record[1:]
        actions = rewrite_actions(record)
        out += event + struct.pack("<I", len(prefix) + len(actions)) + prefix + actions
    out += body[pos:]
    return bytes(out)


def tag_header(code: int, length: int, long_form: bool) -> bytes:
    if length >= 0x3F or long_form:
        return struct.pack("<HI", (code << 6) | 0x3F, length)
    return struct.pack("<H", (code << 6) | length)


def rewrite_tags(data: bytes, pos: int, end: int, version: int, nested: bool = False) -> tuple[bytes, int]:
    out = bytearray()
    while pos + 2 <= end:
        code_len = struct.unpack_from("<H", data, pos)[0]
        pos += 2
        code, length = code_len >> 6, code_len & 0x3F
        long_form = length == 0x3F
        if long_form:
            length = struct.unpack_from("<I", data, pos)[0]
            pos += 4
        body = data[pos:pos + length]
        pos += length
        if code == 12:
            body = rewrite_actions(body)
        elif code == 59:
            body = body[:2] + rewrite_actions(body[2:])
        elif code == 26:
            body = rewrite_place_object2(body, version)
        elif code == 39:
            inner, _ = rewrite_tags(body, 4, len(body), version, nested=True)
            body = body[:4] + inner
        # Keep long headers for tags that must use them (bitmaps etc.).
        out += tag_header(code, len(body), long_form and code not in (12, 59, 26, 39)) + body
        if code == 0:
            break
    return bytes(out), pos


def normalize(swf: bytes) -> bytes:
    version = swf[3]
    nbits = swf[8] >> 3
    header_end = 8 + (5 + nbits * 4 + 7) // 8 + 4
    tags, _ = rewrite_tags(swf, header_end, len(swf), version)
    out = bytearray(swf[:header_end] + tags)
    struct.pack_into("<I", out, 4, len(out))
    return bytes(out)


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--swf", type=Path, default=root / "extracted" / "swf")
    parser.add_argument("--out", type=Path, default=root / "extracted" / "swf_std")
    parser.add_argument("--only", help="process only files whose name contains this")
    args = parser.parse_args()

    done = 0
    for path in sorted(args.swf.rglob("*.swf")):
        if args.only and args.only not in path.name:
            continue
        target = args.out / path.parent.name / path.name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(normalize(path.read_bytes()))
        done += 1
    print(f"normalized {done} SWFs to {args.out}")


if __name__ == "__main__":
    main()

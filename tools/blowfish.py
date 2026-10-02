"""Blowfish (ECB) using the P-array and S-boxes found in castle.exe.

The tables are read from the game's executable, so no cipher constants are
copied into this repository. Blocks can be treated as big-endian (the
reference convention) or little-endian (common in x86 implementations).
"""

from __future__ import annotations

import struct
from pathlib import Path

# Offsets of the tables in the Steam castle.exe.
P_OFFSET = 0xF1228
S_OFFSET = P_OFFSET + 18 * 4
MASK = 0xFFFFFFFF


def load_tables(exe: Path) -> tuple[list[int], list[list[int]]]:
    data = exe.read_bytes()
    p = list(struct.unpack_from("<18I", data, P_OFFSET))
    if p[0] != 0x243F6A88:
        raise ValueError("Blowfish P-array not found at the expected offset")
    s = [list(struct.unpack_from("<256I", data, S_OFFSET + i * 1024)) for i in range(4)]
    return p, s


class Blowfish:
    def __init__(self, key: bytes, tables: tuple[list[int], list[list[int]]], little_endian: bool = False):
        p0, s0 = tables
        self.p = list(p0)
        self.s = [list(box) for box in s0]
        self.little = little_endian
        if not key:
            raise ValueError("empty key")
        for i in range(18):
            k = 0
            for j in range(4):
                k = (k << 8) | key[(i * 4 + j) % len(key)]
            self.p[i] ^= k
        left = right = 0
        for i in range(0, 18, 2):
            left, right = self._encrypt(left, right)
            self.p[i], self.p[i + 1] = left, right
        for box in self.s:
            for i in range(0, 256, 2):
                left, right = self._encrypt(left, right)
                box[i], box[i + 1] = left, right

    def _f(self, x: int) -> int:
        s = self.s
        h = (s[0][x >> 24] + s[1][(x >> 16) & 0xFF]) & MASK
        return ((h ^ s[2][(x >> 8) & 0xFF]) + s[3][x & 0xFF]) & MASK

    def _encrypt(self, left: int, right: int) -> tuple[int, int]:
        p = self.p
        for i in range(16):
            left ^= p[i]
            right ^= self._f(left)
            left, right = right, left
        left, right = right, left
        right ^= p[16]
        left ^= p[17]
        return left, right

    def _decrypt(self, left: int, right: int) -> tuple[int, int]:
        p = self.p
        for i in range(17, 1, -1):
            left ^= p[i]
            right ^= self._f(left)
            left, right = right, left
        left, right = right, left
        right ^= p[1]
        left ^= p[0]
        return left, right

    def decrypt_ecb(self, data: bytes) -> bytes:
        fmt = "<2I" if self.little else ">2I"
        out = bytearray()
        for i in range(0, len(data) - len(data) % 8, 8):
            left, right = struct.unpack_from(fmt, data, i)
            out += struct.pack(fmt, *self._decrypt(left, right))
        return bytes(out)

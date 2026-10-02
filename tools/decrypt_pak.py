"""Decrypts Castle Crashers .pak archives (the Steam PC build).

Each .pak is a ZIP holding one Blowfish-ECB encrypted entry:

- The key is 18 bytes: one of four built-in base keys, chosen by bits 4-5 of
  the entry size, with 4 bytes replaced by a hash of the entry's name (least
  significant byte first) at one of four built-in position sets, chosen by
  bits 6-7 of the size. The hash is h = h * 37 + toupper(c) over the name
  after its last slash, e.g. "EBAT.COK6.NREC".
- Blocks are read as little-endian 32-bit words.
- The decrypted data ends with an 8-byte trailer holding a checksum.

All constants are read from your copy's castle.exe, so nothing from the game
is stored here.

    python tools/extract/decrypt_pak.py --game <the game folder> --out <folder>
"""

from __future__ import annotations

import argparse
import struct
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import blowfish  # noqa: E402

BASE_KEYS_VA = 0x498798      # 4 base keys, 0x18 bytes apart, 18 bytes used
KEY_POSITIONS_VA = 0x4987F8  # 4 sets of 4 key-byte positions
KEY_LENGTH = 18


class Executable:
    def __init__(self, path: Path):
        self.data = path.read_bytes()
        pe = struct.unpack_from("<I", self.data, 0x3C)[0]
        opt = pe + 24
        self.base = struct.unpack_from("<I", self.data, opt + 28)[0]
        count = struct.unpack_from("<H", self.data, pe + 6)[0]
        table = opt + struct.unpack_from("<H", self.data, pe + 20)[0]
        self.sections = []
        for i in range(count):
            vsize, va, rsize, roff = struct.unpack_from("<IIII", self.data, table + i * 40 + 8)
            self.sections.append((va, max(vsize, rsize), roff))

    def read(self, va: int, size: int) -> bytes:
        rva = va - self.base
        for sva, ssize, roff in self.sections:
            if sva <= rva < sva + ssize:
                return self.data[roff + rva - sva: roff + rva - sva + size]
        raise ValueError(f"address {va:#x} is not in the executable")


def name_hash(name: str) -> int:
    base = name.replace("\\", "/").rsplit("/", 1)[-1]
    value = 0
    for ch in base:
        value = (value * 37 + ord(ch.upper())) & 0xFFFFFFFF
    return value


class PakDecryptor:
    def __init__(self, exe_path: Path):
        exe = Executable(exe_path)
        self.base_keys = [exe.read(BASE_KEYS_VA + i * 0x18, KEY_LENGTH) for i in range(4)]
        self.positions = [exe.read(KEY_POSITIONS_VA + i * 4, 4) for i in range(4)]
        self.tables = blowfish.load_tables(exe_path)

    def key(self, size: int, seed: int) -> bytes:
        selector = (size >> 4) & 0xFF
        key = bytearray(self.base_keys[selector & 3])
        for position in self.positions[(selector & 0xF) >> 2]:
            key[position] = seed & 0xFF
            seed >>= 8
        return bytes(key)

    def decrypt(self, data: bytes, entry_name: str) -> bytes:
        cipher = blowfish.Blowfish(self.key(len(data), name_hash(entry_name)), self.tables, little_endian=True)
        return cipher.decrypt_ecb(data)


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--game", type=Path, default=root / "CastleCrashers")
    parser.add_argument("--out", type=Path, default=root / "extracted" / "pak")
    parser.add_argument("--only", help="decrypt only archives whose name contains this")
    args = parser.parse_args()

    decryptor = PakDecryptor(args.game / "castle.exe")
    count = 0
    for pak in sorted((args.game / "data").rglob("*.pak")):
        if args.only and args.only not in pak.name:
            continue
        with zipfile.ZipFile(pak) as archive:
            for info in archive.infolist():
                plain = decryptor.decrypt(archive.read(info), info.filename)
                name = info.filename.removesuffix(".NREC").lower()
                target = args.out / pak.parent.name / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(plain)
                count += 1
    print(f"decrypted {count} entries to {args.out}")


if __name__ == "__main__":
    main()

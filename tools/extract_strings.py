"""Extracts the game's localized text from castle.exe.

Scripts show text by number (`txtUseController.ntext = 742`). The strings are
compiled into the executable as UTF-16: a table of 778 records, each a u32
string number followed by one pointer per language. This writes
<out>/<language>.txt with one
`number<TAB>text` line per string (backslash, tab and newline escaped as
\\\\, \\t and \\n).

    python tools/extract/extract_strings.py --exe <the game's castle.exe> --out <folder>
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path



class PeImage:
    """The executable's image base and sections (virtual size, address, raw size, file offset)."""

    def __init__(self, data: bytes):
        self.data = data
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        opt = pe + 24
        self.base = struct.unpack_from("<I", data, opt + 28)[0]
        count = struct.unpack_from("<H", data, pe + 6)[0]
        table = opt + struct.unpack_from("<H", data, pe + 20)[0]
        self.sections = [struct.unpack_from("<IIII", data, table + i * 40 + 8) for i in range(count)]

LANGUAGES = ["en", "de", "fr", "es", "it", "ja", "ko", "zh-Hant", "pt", "zh-Hans"]
RECORD = 4 + 4 * len(LANGUAGES)
KNOWN_RECORD = 0xFA398  # string 742, "We recommend a controller to play!"


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", type=Path, default=root / "CastleCrashers" / "castle.exe")
    parser.add_argument("--out", type=Path, default=root / "assets" / "text")
    args = parser.parse_args()

    data = args.exe.read_bytes()
    image = PeImage(data)

    def file_offset(va: int) -> int | None:
        rva = va - image.base
        for vsize, sva, rsize, raw in image.sections:
            if sva <= rva < sva + max(vsize, rsize):
                return raw + rva - sva
        return None

    def is_record(pos: int) -> bool:
        fields = struct.unpack_from(f"<{1 + len(LANGUAGES)}I", data, pos)
        return fields[0] < 0x10000 and all(file_offset(p) is not None for p in fields[1:])

    def utf16(va: int) -> str:
        start = end = file_offset(va)
        while data[end:end + 2] != b"\0\0":
            end += 2
        return data[start:end].decode("utf-16-le")

    # Walk out from a known record to the ends of the table.
    first = last = KNOWN_RECORD
    while is_record(first - RECORD):
        first -= RECORD
    while is_record(last + RECORD):
        last += RECORD

    table: dict[str, list[str]] = {lang: [] for lang in LANGUAGES}
    for pos in range(first, last + RECORD, RECORD):
        number, *pointers = struct.unpack_from(f"<{1 + len(LANGUAGES)}I", data, pos)
        for lang, ptr in zip(LANGUAGES, pointers):
            text = utf16(ptr).replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n").replace("\r", "")
            table[lang].append(f"{number}\t{text}")

    args.out.mkdir(parents=True, exist_ok=True)
    for lang, lines in table.items():
        with open(args.out / f"{lang}.txt", "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(lines) + "\n")
    print(f"{(last - first) // RECORD + 1} strings in {len(LANGUAGES)} languages written to {args.out}")


if __name__ == "__main__":
    main()

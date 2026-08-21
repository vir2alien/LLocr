#!/usr/bin/env python3
"""Build a Windows .ico from PNG icons.

ICO can store each entry either as uncompressed 32-bit BGRA or as a PNG stream.
PNG-compressed entries are supported since Windows Vista and are what modern
tooling emits; Explorer/Taskbar render them at all sizes.  See:
  - https://en.wikipedia.org/wiki/ICO_(file_format)
  - Microsoft "Icon and Cursor Resources" documentation

Usage:
  make_ico.py out.ico icon{16,24,32,48,64,128,256}.png...
"""

import struct
import sys


def parse_png_size(data: bytes) -> int:
    if not data.startswith(b'\x89PNG\r\n\x1a\n'):
        raise ValueError('not a PNG')
    return struct.unpack('>I', data[16:20])[0]


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    out_path = sys.argv[1]
    inputs = sys.argv[2:]

    entries = []  # (encoded_data, width_byte, height_byte)
    for p in inputs:
        data = open(p, 'rb').read()
        size = parse_png_size(data)
        w = 0 if size >= 256 else size
        h = 0 if size >= 256 else size
        entries.append((data, w, h))

    entries.sort(key=lambda e: e[1])  # ascending size

    if len(entries) > 16:
        print('too many entries (max 16)', file=sys.stderr)
        return 1

    out = bytearray(struct.pack('<HHH', 0, 1, len(entries)))
    offset = 6 + 16 * len(entries)
    for data, w, h in entries:
        out += struct.pack('<BBBBHHII', w, h, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    for data, _, _ in entries:
        out += data

    with open(out_path, 'wb') as f:
        f.write(out)
    print(f'wrote {out_path} ({len(out)} bytes, {len(entries)} entries)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
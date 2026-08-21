#!/usr/bin/env python3
"""Build an .icns from PNG icons.

The .icns format is a simple container: a 4-byte magic 'icns', a 4-byte big-
endian total length, then a sequence of chunks.  Each chunk is a 4-byte type
code, a 4-byte big-endian length (including the 8-byte header), and a payload.

For the sizes listed below the payload is a PNG image, which modern macOS
(10.14+) accepts for every entry.  See:
  - Apple: iconutil + Icon Composer
  - https://en.wikipedia.org/wiki/Apple_Icon_Image_format

Usage:
  make_icns.py out.icns icon{16,32,64,128,256,512,1024}.png...
"""

import struct
import sys
import os

# (type_code, expected_pixel_size)
ENTRIES = [
    (b'icp4', 16),
    (b'icp5', 32),
    (b'icp6', 64),
    (b'ic07', 128),
    (b'ic08', 256),
    (b'ic09', 512),
    (b'ic10', 1024),
]


def parse_png_size(data: bytes) -> int:
    # IHDR is at offset 8; width is the first big-endian uint32 at offset 16.
    if not data.startswith(b'\x89PNG\r\n\x1a\n'):
        raise ValueError('not a PNG')
    return struct.unpack('>I', data[16:20])[0]


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    out_path = sys.argv[1]
    pngs = sys.argv[2:]
    chunks = []
    for code, want in ENTRIES:
        match = [p for p in pngs if parse_png_size(open(p, 'rb').read()) == want]
        if not match:
            print(f'warning: no {want}x{want} PNG provided; skipping {code}', file=sys.stderr)
            continue
        data = open(match[0], 'rb').read()
        chunks.append((code, data))

    if not chunks:
        print('no usable PNGs', file=sys.stderr)
        return 1

    total = 8 + sum(8 + len(d) for _, d in chunks)
    out = bytearray()
    out += b'icns' + struct.pack('>I', total)
    for code, data in chunks:
        out += code + struct.pack('>I', 8 + len(data)) + data
    with open(out_path, 'wb') as f:
        f.write(out)
    print(f'wrote {out_path} ({total} bytes, {len(chunks)} chunks)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
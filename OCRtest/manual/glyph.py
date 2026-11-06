#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Метрика масштаба кропа: медианная высота буквенных связных компонент.

Та же метрика, что в эксперименте (tmp/scripts/glyphs.py): цель READ для
формул — кегль ~33 px (эталон 5.png); кроп приложения при 300 DPI даёт
~18 px, и на нём модель теряет акценты и мелкие знаки.

usage: py glyph.py <image.png> [image2.png ...]
Большие картинки считает медленно (чистый Python) — это инструмент ручной.
"""

import sys
from collections import deque

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

try:
    from PIL import Image
except ImportError:
    sys.exit("нужен Pillow: pip install pillow")

THRESHOLD = 128  # как в эксперименте


def components(mask, w, h):
    seen = bytearray(w * h)
    out = []
    for start in range(w * h):
        if not mask[start] or seen[start]:
            continue
        seen[start] = 1
        queue = deque([start])
        y0 = y1 = start // w
        x0 = x1 = start % w
        area = 0
        while queue:
            pos = queue.popleft()
            area += 1
            y, x = divmod(pos, w)
            y0 = min(y0, y); y1 = max(y1, y)
            x0 = min(x0, x); x1 = max(x1, x)
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    ny, nx = y + dy, x + dx
                    if 0 <= ny < h and 0 <= nx < w:
                        npos = ny * w + nx
                        if mask[npos] and not seen[npos]:
                            seen[npos] = 1
                            queue.append(npos)
        out.append((y1 - y0 + 1, x1 - x0 + 1, area))
    return out


def glyph_height(path):
    img = Image.open(path).convert("L")
    w, h = img.size
    mask = bytearray(b < THRESHOLD for b in img.getdata())
    comps = components(mask, w, h)
    letters = sorted(c[0] for c in comps
                     if 4 <= c[0] <= 0.9 * h and c[1] <= 0.6 * w and c[2] >= 8)
    if not letters:
        return 0.0, (w, h)
    return float(letters[len(letters) // 2]), (w, h)


def main():
    paths = sys.argv[1:]
    if not paths:
        sys.exit("usage: py glyph.py <image.png> [...]")
    for path in paths:
        glyph, (w, h) = glyph_height(path)
        if glyph == 0:
            verdict = "буквенных знаков не найдено"
        elif 28 <= glyph <= 40:
            verdict = "≈ цель READ, так и должно быть"
        elif glyph < 24:
            verdict = "масштаб приложения: мелкая печать будет читаться с ошибками"
        else:
            verdict = "крупнее цели — допустимо, токенов больше"
        print(f"{path}: {w}x{h}, кегль {glyph:.0f} px — {verdict}")


if __name__ == "__main__":
    main()

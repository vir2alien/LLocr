#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Порт src/models/CropScaling.cpp::cropGlyphHeight — высота буквы кропа.

Нужен стенду prompt_ab.py: приложение (ADR 287) масштабирует кроп формулы
до 32 px высоты буквы, стенд должен видеть те же картинки. Отличие от C++:
масштабирование делает PIL, а не QImage::scaled — байты не совпадут с
приложением, для сравнения промптов это не важно (все варианты получают
одинаковую картинку).
"""

import numpy as np
from PIL import Image

K_MIN_LETTER_HEIGHT = 4
K_MAX_LETTER_HEIGHT = 0.9  # of the crop height: rules, borders
K_MAX_LETTER_WIDTH = 0.6   # of the crop width: fraction bars
K_MIN_LETTER_AREA = 8
K_MIN_GLYPH_UPSCALE = 1.05


def otsu_threshold(gray):
    hist, _ = np.histogram(gray, bins=256, range=(0, 256))
    total = gray.size
    weighted_sum = np.dot(np.arange(256), hist)
    best_variance = -1.0
    threshold = 128
    background_weight = 0.0
    background_sum = 0.0
    for i in range(256):
        background_weight += hist[i]
        if background_weight <= 0:
            continue
        foreground_weight = total - background_weight
        if foreground_weight <= 0:
            break
        background_sum += i * hist[i]
        background_mean = background_sum / background_weight
        foreground_mean = (weighted_sum - background_sum) / foreground_weight
        spread = background_mean - foreground_mean
        variance = background_weight * foreground_weight * spread * spread
        if variance > best_variance:
            best_variance = variance
            threshold = i + 1
    return threshold


def components(mask):
    """8-связные компоненты: объединение отрезков чернил по строкам."""
    h, w = mask.shape
    parent = {}

    def find(x):
        root = x
        while parent[root] != root:
            root = parent[root]
        while parent[x] != root:
            parent[x], x = root, parent[x]
        return root

    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[max(ra, rb)] = min(ra, rb)

    bounds = {}  # root -> [minX, maxX, minY, maxY, area]
    prev_runs = []  # (start, end, root) прошлого ряда
    for y in range(h):
        row = mask[y]
        runs = []
        x = 0
        while x < w:
            if not row[x]:
                x += 1
                continue
            start = x
            while x < w and row[x]:
                x += 1
            runs.append((start, x - 1))

        merged = []
        for (s, e) in runs:
            root = None
            for (ps, pe, proot) in prev_runs:
                if ps <= e + 1 and s <= pe + 1:  # 8-связность: соседняя колонка
                    if root is None:
                        root = find(proot)
                    else:
                        union(root, proot)
                        root = find(root)
            if root is None:
                root = len(parent)
                parent[root] = root
                bounds[root] = [s, s, y, y, 0]
            merged.append((s, e, root))
        prev_runs = merged
        for (s, e, root) in merged:
            root = find(root)
            if root not in bounds:
                bounds[root] = [s, e, y, y, 0]
            b = bounds[root]
            b[0] = min(b[0], s)
            b[1] = max(b[1], e)
            b[2] = min(b[2], y)
            b[3] = max(b[3], y)
            b[4] += e - s + 1

    out = []
    for root, (min_x, max_x, min_y, max_y, area) in bounds.items():
        if find(root) != root:
            continue
        out.append((max_y - min_y + 1, max_x - min_x + 1, area))
    return out


def crop_glyph_height(crop):
    """Медианная высота «буквы» кропа (0 — нет чернил/компонент)."""
    if crop.width < K_MIN_LETTER_HEIGHT or crop.height < K_MIN_LETTER_HEIGHT:
        return 0
    gray = np.asarray(crop.convert("L"), dtype=np.uint8)
    threshold = otsu_threshold(gray)
    mask = gray < threshold
    if not mask.any():
        return 0
    heights = []
    for height, width, area in components(mask):
        if (height >= K_MIN_LETTER_HEIGHT and height <= K_MAX_LETTER_HEIGHT * gray.shape[0]
                and width <= K_MAX_LETTER_WIDTH * gray.shape[1] and area >= K_MIN_LETTER_AREA):
            heights.append(height)
    if not heights:
        return 0
    return int(np.median(heights))


def apply_glyph_stage(crop, target):
    """applyCropScaling для одной цели высоты: только вверх, scale >= 1.05."""
    if not target:
        return crop
    glyph = crop_glyph_height(crop)
    if glyph <= 0 or glyph >= target:
        return crop
    scale = target / glyph
    if scale < K_MIN_GLYPH_UPSCALE:
        return crop
    new_size = (max(1, round(crop.width * scale)), max(1, round(crop.height * scale)))
    return crop.resize(new_size, Image.BILINEAR)


if __name__ == "__main__":
    import json
    import sys
    from pathlib import Path

    HERE = Path(__file__).resolve().parent
    corpus = json.loads((HERE / "corpus" / "corpus_equations.json").read_text(encoding="utf-8"))
    target = 32
    if len(sys.argv) > 1:
        target = int(sys.argv[1])
    scaled = 0
    for block in corpus["blocks"]:
        crop = Image.open(HERE / block["crop"])
        crop.load()
        glyph = crop_glyph_height(crop)
        scale_note = ""
        if block["label"] == "equation" and 0 < glyph < target:
            s = target / glyph
            scale_note = f"  -> x{s:.2f}" if s >= K_MIN_GLYPH_UPSCALE else "  (масштаб < 1.05, нет)"
            scaled += 1 if s >= K_MIN_GLYPH_UPSCALE else 0
        print(f"{block['uid']:10s} {block['label']:9s} glyph={glyph:3d} size={crop.width}x{crop.height}{scale_note}")
    print(f"\nбудет масштабировано (equation, до {target} px): {scaled}")

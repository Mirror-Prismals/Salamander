#!/usr/bin/env python3
"""
Create a copy of an atlas PNG with tile index numbers overlaid on every tile.

Uses the local PNG helpers in rebuild_atlas.py (no external dependencies).
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from rebuild_atlas import read_png_rgba, write_png_rgba


# 3x5 bitmap digits.
FONT_3X5 = {
    "0": ("111", "101", "101", "101", "111"),
    "1": ("010", "110", "010", "010", "111"),
    "2": ("111", "001", "111", "100", "111"),
    "3": ("111", "001", "111", "001", "111"),
    "4": ("101", "101", "111", "001", "001"),
    "5": ("111", "100", "111", "001", "111"),
    "6": ("111", "100", "111", "101", "111"),
    "7": ("111", "001", "001", "001", "001"),
    "8": ("111", "101", "111", "101", "111"),
    "9": ("111", "101", "111", "001", "111"),
}


def clamp_u8(v: float) -> int:
    if v <= 0.0:
        return 0
    if v >= 255.0:
        return 255
    return int(v + 0.5)


def blend_pixel(buf: bytearray, img_w: int, x: int, y: int, src_rgba: tuple[int, int, int, int]) -> None:
    if x < 0 or y < 0:
        return
    idx = (y * img_w + x) * 4
    if idx < 0 or idx + 3 >= len(buf):
        return

    sr, sg, sb, sa8 = src_rgba
    if sa8 <= 0:
        return

    dr = float(buf[idx + 0])
    dg = float(buf[idx + 1])
    db = float(buf[idx + 2])
    da = float(buf[idx + 3]) / 255.0

    sa = float(sa8) / 255.0
    out_a = sa + da * (1.0 - sa)
    if out_a <= 1e-6:
        buf[idx + 0] = 0
        buf[idx + 1] = 0
        buf[idx + 2] = 0
        buf[idx + 3] = 0
        return

    out_r = (float(sr) * sa + dr * da * (1.0 - sa)) / out_a
    out_g = (float(sg) * sa + dg * da * (1.0 - sa)) / out_a
    out_b = (float(sb) * sa + db * da * (1.0 - sa)) / out_a

    buf[idx + 0] = clamp_u8(out_r)
    buf[idx + 1] = clamp_u8(out_g)
    buf[idx + 2] = clamp_u8(out_b)
    buf[idx + 3] = clamp_u8(out_a * 255.0)


def fill_rect(buf: bytearray, img_w: int, img_h: int, x: int, y: int, w: int, h: int, color: tuple[int, int, int, int]) -> None:
    if w <= 0 or h <= 0:
        return
    x0 = max(0, x)
    y0 = max(0, y)
    x1 = min(img_w, x + w)
    y1 = min(img_h, y + h)
    if x0 >= x1 or y0 >= y1:
        return
    for py in range(y0, y1):
        for px in range(x0, x1):
            blend_pixel(buf, img_w, px, py, color)


def draw_digit(buf: bytearray,
               img_w: int,
               img_h: int,
               x: int,
               y: int,
               digit: str,
               scale: int,
               color: tuple[int, int, int, int]) -> None:
    pattern = FONT_3X5.get(digit)
    if not pattern:
        return
    for gy, row in enumerate(pattern):
        for gx, bit in enumerate(row):
            if bit != "1":
                continue
            px = x + gx * scale
            py = y + gy * scale
            fill_rect(buf, img_w, img_h, px, py, scale, scale, color)


def draw_text(buf: bytearray,
              img_w: int,
              img_h: int,
              x: int,
              y: int,
              text: str,
              scale: int,
              color: tuple[int, int, int, int]) -> None:
    advance = 3 * scale + scale  # glyph width + spacing
    cx = x
    for ch in text:
        draw_digit(buf, img_w, img_h, cx, y, ch, scale, color)
        cx += advance


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--atlas-json", default="Procedures/assets/atlas_v7.json")
    parser.add_argument("--atlas-png", default="Procedures/assets/atlas_v7.png")
    parser.add_argument("--out-png", default="Procedures/assets/atlas_v7_indexed.png")
    parser.add_argument("--scale", type=int, default=2)
    args = parser.parse_args()

    atlas_json = Path(args.atlas_json)
    atlas_png = Path(args.atlas_png)
    out_png = Path(args.out_png)

    data = json.loads(atlas_json.read_text())
    tile_w, tile_h = data["tileSize"]
    cols = int(data["tilesPerRow"])
    rows = int(data["tilesPerCol"])
    atlas_w, atlas_h = data["atlasSize"]

    img_w, img_h, img_rgba = read_png_rgba(atlas_png)
    if img_w != atlas_w or img_h != atlas_h:
        raise ValueError("atlas image dimensions do not match atlas json")

    scale = max(1, int(args.scale))
    if tile_w < 16 or tile_h < 16:
        scale = 1

    out = bytearray(img_rgba)
    total = cols * rows

    for tile_index in range(total):
        tx = (tile_index % cols) * tile_w
        ty = (tile_index // cols) * tile_h
        label = str(tile_index)

        text_w = len(label) * (3 * scale + scale) - scale
        text_h = 5 * scale

        # Top-left badge.
        bx = tx + 1
        by = ty + 1
        pad = 1
        fill_rect(out, img_w, img_h, bx - pad, by - pad, text_w + pad * 2, text_h + pad * 2, (0, 0, 0, 160))

        # Shadow + text.
        draw_text(out, img_w, img_h, bx + 1, by + 1, label, scale, (0, 0, 0, 220))
        draw_text(out, img_w, img_h, bx, by, label, scale, (255, 255, 255, 255))

    write_png_rgba(out_png, img_w, img_h, bytes(out))
    print(f"wrote: {out_png}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


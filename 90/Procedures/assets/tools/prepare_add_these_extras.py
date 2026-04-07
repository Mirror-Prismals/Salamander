#!/usr/bin/env python3
"""
Prepare extra textures for atlas ingestion in Procedures/assets/add_these.

What it does:
- Converts the 8 non-matching source PNGs into 24x24 files with 24x24_ names.
- Copies Procedures/assets/24x24_chalk_floor_cross_rgba_v002.png into add_these.

Uses rebuild_atlas.py's PNG read/write helpers to avoid external dependencies.
"""

from __future__ import annotations

import argparse
from pathlib import Path

from rebuild_atlas import read_png_rgba, write_png_rgba


EXTRA_TEXTURES = [
    ("240x240_gemcutting_table_top_v001.png", "24x24_gemcutting_table_top_v001.png"),
    ("240x240_zelda_cave_pot_side_plain_rgba_v001.png", "24x24_zelda_cave_pot_side_plain_rgba_v001.png"),
    ("axehead_stencil.png", "24x24_axehead_stencil.png"),
    ("hilt_stencil.png", "24x24_hilt_stencil.png"),
    ("pickaxe_stencil.png", "24x24_pickaxe_stencil.png"),
    ("scythe_stencil.png", "24x24_scythe_stencil.png"),
    ("spade_stencil.png", "24x24_spade_stencil.png"),
    ("sword_stencil.png", "24x24_sword_stencil.png"),
]


def resize_nearest_rgba(src_rgba: bytes, src_w: int, src_h: int, dst_w: int, dst_h: int) -> bytes:
    dst = bytearray(dst_w * dst_h * 4)
    for y in range(dst_h):
        sy = min(src_h - 1, (y * src_h) // dst_h)
        for x in range(dst_w):
            sx = min(src_w - 1, (x * src_w) // dst_w)
            sidx = (sy * src_w + sx) * 4
            didx = (y * dst_w + x) * 4
            dst[didx : didx + 4] = src_rgba[sidx : sidx + 4]
    return bytes(dst)


def ensure_24x24_png(src: Path, dst: Path, overwrite: bool) -> str:
    if not src.exists():
        raise FileNotFoundError(f"missing source: {src}")
    if dst.exists() and not overwrite:
        return f"skip  : {dst.name} (exists)"

    w, h, rgba = read_png_rgba(src)
    if w == 24 and h == 24:
        out = rgba
    else:
        out = resize_nearest_rgba(rgba, w, h, 24, 24)
    write_png_rgba(dst, 24, 24, out)
    return f"write : {dst.name} (from {src.name}, {w}x{h} -> 24x24)"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--assets-dir", default="Procedures/assets")
    parser.add_argument("--add-these-dir", default="Procedures/assets/add_these")
    parser.add_argument("--overwrite", action="store_true")
    args = parser.parse_args()

    assets_dir = Path(args.assets_dir)
    add_these_dir = Path(args.add_these_dir)
    add_these_dir.mkdir(parents=True, exist_ok=True)

    for src_name, dst_name in EXTRA_TEXTURES:
        src = add_these_dir / src_name
        dst = add_these_dir / dst_name
        print(ensure_24x24_png(src, dst, args.overwrite))

    chalk_name = "24x24_chalk_floor_cross_rgba_v002.png"
    chalk_src = assets_dir / chalk_name
    chalk_dst = add_these_dir / chalk_name
    print(ensure_24x24_png(chalk_src, chalk_dst, args.overwrite))

    print("done")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

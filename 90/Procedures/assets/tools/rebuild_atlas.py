#!/usr/bin/env python3
"""
Rebuilds a texture atlas from a base atlas + all 24x24_* textures in Procedures/assets.

Design goals:
- Keep old atlas files as a backup safeguard.
- Preserve existing tile indices from the base atlas.
- Append new tiles for discovered 24x24 textures.
- Update atlas JSON mappings deterministically.
- Avoid external Python dependencies (pure stdlib PNG read/write).
"""

from __future__ import annotations

import argparse
import json
import math
import shutil
import struct
import zlib
from pathlib import Path
from typing import Dict, List, Tuple


PNG_SIG = b"\x89PNG\r\n\x1a\n"


def paeth_predictor(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def read_png_rgba(path: Path) -> Tuple[int, int, bytes]:
    data = path.read_bytes()
    if len(data) < 8 or data[:8] != PNG_SIG:
        raise ValueError(f"{path}: invalid PNG signature")

    pos = 8
    width = height = 0
    bit_depth = color_type = interlace = None
    idat = bytearray()

    while pos + 8 <= len(data):
        length = struct.unpack(">I", data[pos : pos + 4])[0]
        pos += 4
        ctype = data[pos : pos + 4]
        pos += 4
        chunk = data[pos : pos + length]
        pos += length
        _crc = data[pos : pos + 4]
        pos += 4

        if ctype == b"IHDR":
            width, height, bit_depth, color_type, _comp, _filt, interlace = struct.unpack(
                ">IIBBBBB", chunk
            )
        elif ctype == b"IDAT":
            idat.extend(chunk)
        elif ctype == b"IEND":
            break

    if width <= 0 or height <= 0:
        raise ValueError(f"{path}: missing IHDR")
    if bit_depth != 8:
        raise ValueError(f"{path}: unsupported bit depth {bit_depth}, expected 8")
    if interlace != 0:
        raise ValueError(f"{path}: interlaced PNG not supported")
    if color_type not in (2, 6):
        raise ValueError(f"{path}: unsupported color type {color_type} (expected RGB or RGBA)")

    bpp = 4 if color_type == 6 else 3
    row_bytes = width * bpp
    raw = zlib.decompress(bytes(idat))
    expected = (row_bytes + 1) * height
    if len(raw) != expected:
        raise ValueError(
            f"{path}: unexpected decompressed length {len(raw)} (expected {expected})"
        )

    scan = bytearray(width * height * bpp)
    prior = bytearray(row_bytes)
    src = 0
    dst = 0
    for _ in range(height):
        ftype = raw[src]
        src += 1
        cur = bytearray(raw[src : src + row_bytes])
        src += row_bytes

        if ftype == 1:  # Sub
            for i in range(row_bytes):
                left = cur[i - bpp] if i >= bpp else 0
                cur[i] = (cur[i] + left) & 0xFF
        elif ftype == 2:  # Up
            for i in range(row_bytes):
                cur[i] = (cur[i] + prior[i]) & 0xFF
        elif ftype == 3:  # Average
            for i in range(row_bytes):
                left = cur[i - bpp] if i >= bpp else 0
                up = prior[i]
                cur[i] = (cur[i] + ((left + up) // 2)) & 0xFF
        elif ftype == 4:  # Paeth
            for i in range(row_bytes):
                left = cur[i - bpp] if i >= bpp else 0
                up = prior[i]
                up_left = prior[i - bpp] if i >= bpp else 0
                cur[i] = (cur[i] + paeth_predictor(left, up, up_left)) & 0xFF
        elif ftype != 0:
            raise ValueError(f"{path}: unsupported PNG filter {ftype}")

        scan[dst : dst + row_bytes] = cur
        dst += row_bytes
        prior = cur

    if color_type == 6:
        return width, height, bytes(scan)

    # RGB -> RGBA
    rgba = bytearray(width * height * 4)
    src = 0
    dst = 0
    for _ in range(width * height):
        rgba[dst] = scan[src]
        rgba[dst + 1] = scan[src + 1]
        rgba[dst + 2] = scan[src + 2]
        rgba[dst + 3] = 255
        src += 3
        dst += 4
    return width, height, bytes(rgba)


def _png_chunk(chunk_type: bytes, payload: bytes) -> bytes:
    crc = zlib.crc32(chunk_type)
    crc = zlib.crc32(payload, crc) & 0xFFFFFFFF
    return struct.pack(">I", len(payload)) + chunk_type + payload + struct.pack(">I", crc)


def write_png_rgba(path: Path, width: int, height: int, rgba: bytes) -> None:
    if len(rgba) != width * height * 4:
        raise ValueError("RGBA byte length mismatch")

    raw = bytearray()
    stride = width * 4
    for y in range(height):
        row = rgba[y * stride : (y + 1) * stride]
        raw.append(0)  # filter type None
        raw.extend(row)
    comp = zlib.compress(bytes(raw), level=9)

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    out = bytearray(PNG_SIG)
    out.extend(_png_chunk(b"IHDR", ihdr))
    out.extend(_png_chunk(b"IDAT", comp))
    out.extend(_png_chunk(b"IEND", b""))
    path.write_bytes(bytes(out))


def extract_tile(
    rgba: bytes, img_w: int, img_h: int, tile_x: int, tile_y: int, tile_w: int, tile_h: int
) -> bytes:
    # tile_y is top-based
    out = bytearray(tile_w * tile_h * 4)
    out_row_bytes = tile_w * 4
    in_row_bytes = img_w * 4
    for row in range(tile_h):
        src_y = tile_y + row
        if not (0 <= src_y < img_h):
            raise ValueError("tile extraction out of bounds (y)")
        src = src_y * in_row_bytes + tile_x * 4
        dst = row * out_row_bytes
        out[dst : dst + out_row_bytes] = rgba[src : src + out_row_bytes]
    return bytes(out)


def blit_tile(
    dst_rgba: bytearray,
    atlas_w: int,
    atlas_h: int,
    tile: bytes,
    tile_x: int,
    tile_y: int,
    tile_w: int,
    tile_h: int,
) -> None:
    in_row_bytes = tile_w * 4
    dst_row_bytes = atlas_w * 4
    for row in range(tile_h):
        dst_y = tile_y + row
        if not (0 <= dst_y < atlas_h):
            raise ValueError("tile blit out of bounds (y)")
        src = row * in_row_bytes
        dst = dst_y * dst_row_bytes + tile_x * 4
        dst_rgba[dst : dst + in_row_bytes] = tile[src : src + in_row_bytes]


def camel_case_from_stem(stem: str) -> str:
    parts = [p for p in stem.split("_") if p]
    return "".join(p[:1].upper() + p[1:] for p in parts)


def normalize_mapping_for_key(blocks: Dict[str, Dict[str, int]], key: str, index: int) -> None:
    blocks[key] = {"all": index}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--assets-dir", default="Procedures/assets")
    parser.add_argument("--base-json", default="Procedures/assets/atlas.json")
    parser.add_argument("--base-png", default="Procedures/assets/atlas.png")
    parser.add_argument("--out-json", default="Procedures/assets/atlas_v2.json")
    parser.add_argument("--out-png", default="Procedures/assets/atlas_v2.png")
    parser.add_argument("--backup-json", default="Procedures/assets/atlas_legacy_backup.json")
    parser.add_argument("--backup-png", default="Procedures/assets/atlas_legacy_backup.png")
    parser.add_argument("--tiles-per-row", type=int, default=8)
    args = parser.parse_args()

    assets_dir = Path(args.assets_dir)
    base_json = Path(args.base_json)
    base_png = Path(args.base_png)
    out_json = Path(args.out_json)
    out_png = Path(args.out_png)
    backup_json = Path(args.backup_json)
    backup_png = Path(args.backup_png)

    if not backup_json.exists():
        shutil.copy2(base_json, backup_json)
        print(f"backup: {backup_json}")
    if not backup_png.exists():
        shutil.copy2(base_png, backup_png)
        print(f"backup: {backup_png}")

    atlas_data = json.loads(base_json.read_text())
    tile_w, tile_h = atlas_data["tileSize"]
    old_cols = int(atlas_data["tilesPerRow"])
    old_rows = int(atlas_data["tilesPerCol"])
    old_tile_count = old_cols * old_rows
    old_w = int(atlas_data["atlasSize"][0])
    old_h = int(atlas_data["atlasSize"][1])
    if old_w != old_cols * tile_w or old_h != old_rows * tile_h:
        raise ValueError("atlas size mismatch with tile grid")

    img_w, img_h, img_rgba = read_png_rgba(base_png)
    if img_w != old_w or img_h != old_h:
        raise ValueError("base atlas PNG dimensions do not match atlas.json")

    blocks = dict(atlas_data.get("blocks", {}))
    tiles: List[bytes] = []
    for idx in range(old_tile_count):
        tx = (idx % old_cols) * tile_w
        ty = (idx // old_cols) * tile_h  # top-based
        tiles.append(extract_tile(img_rgba, img_w, img_h, tx, ty, tile_w, tile_h))

    # Explicit key mapping for known in-project textures.
    file_to_key = {
        "24x24_amethyst_dirt_ore_combined.png": "AmethystOre",
        "24x24_ruby_dirt_ore_combined.png": "RubyOre",
        "24x24_rainbow_fluorite_dirt_ore_combined.png": "FlouriteOre",
        "24x24_silver_dirt_ore_combined.png": "SilverOre",
        "24x24_dirt_texture.png": "DirtExternal",
        "24x24_dirt_texture_4A3621.png": "StoneExternal",
        "24x24_grass_block_top_v007.png": "Grass",
        "24x24_cobblestone_climbing_holds.png": "CobblestoneClimbingHolds",
        "24x24_cobblestone_ryb_blue.png": "CobblestoneRYBBlue",
        "24x24_cobblestone_ryb_red.png": "CobblestoneRYBRed",
        "24x24_cobblestone_ryb_yellow.png": "CobblestoneRYBYellow",
        "24x24_lilypad_v001.png": "LilypadV001",
        "24x24_tall_grass_side_v001.png": "TallGrassV001",
        "24x24_tall_grass_side_v002.png": "TallGrassV002",
        "24x24_tall_grass_side_v003.png": "TallGrassV003",
        "24x24_short_grass_side_v001.png": "ShortGrassV001",
        "24x24_short_grass_side_v002.png": "ShortGrassV002",
        "24x24_short_grass_side_v003.png": "ShortGrassV003",
    }
    file_aliases = {
        "24x24_grass_block_top_v007.png": ["GrassTopV007"],
    }

    discovered = sorted(assets_dir.glob("24x24_*.png"))
    if not discovered:
        raise ValueError("No 24x24_*.png textures found in assets directory")

    for tex_path in discovered:
        tw, th, trgba = read_png_rgba(tex_path)
        if tw != tile_w or th != tile_h:
            raise ValueError(f"{tex_path.name}: expected {tile_w}x{tile_h}, got {tw}x{th}")
        tile_index = len(tiles)
        tiles.append(trgba)

        key = file_to_key.get(tex_path.name)
        if key is None:
            key = camel_case_from_stem(tex_path.stem)
        normalize_mapping_for_key(blocks, key, tile_index)
        for alias in file_aliases.get(tex_path.name, []):
            normalize_mapping_for_key(blocks, alias, tile_index)
        print(f"tile {tile_index:3d}: {tex_path.name} -> {key}")

    cols = max(1, int(args.tiles_per_row))
    rows = int(math.ceil(len(tiles) / cols))
    out_w = cols * tile_w
    out_h = rows * tile_h
    out_rgba = bytearray(out_w * out_h * 4)

    for idx, tile in enumerate(tiles):
        tx = (idx % cols) * tile_w
        ty = (idx // cols) * tile_h  # top-based
        blit_tile(out_rgba, out_w, out_h, tile, tx, ty, tile_w, tile_h)

    write_png_rgba(out_png, out_w, out_h, bytes(out_rgba))

    out_data = {
        "tileSize": [tile_w, tile_h],
        "atlasSize": [out_w, out_h],
        "tilesPerRow": cols,
        "tilesPerCol": rows,
        "blocks": blocks,
    }
    out_json.write_text(json.dumps(out_data, indent=2) + "\n")

    print(f"wrote: {out_png} ({out_w}x{out_h}, tiles={len(tiles)})")
    print(f"wrote: {out_json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


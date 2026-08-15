#!/usr/bin/env python3
"""Split a leafset atlas into individual leaf textures using the opacity map.

Uses connected-component labeling on the opacity map to find each leaf's
bounding box, then crops ALL PBR maps to those regions. The Color map gets
the opacity baked in as an alpha channel (RGBA); other maps are saved as-is
with the same crop.
"""

import sys
from pathlib import Path
from collections import deque

from PIL import Image

OPACITY_THRESHOLD = 128  # pixel > this counts as opaque
PADDING = 4              # extra pixels around bounding box

# PBR maps to split, keyed by short name.
# "mode" is the Pillow mode to convert to before cropping.
# "alpha" means the opacity map is applied as the alpha channel.
PBR_MAPS = {
    "color":        {"suffix": "Color",        "mode": "RGB",  "alpha": True},
    "normal_gl":    {"suffix": "NormalGL",     "mode": "RGB",  "alpha": False},
    "normal_dx":    {"suffix": "NormalDX",     "mode": "RGB",  "alpha": False},
    "roughness":    {"suffix": "Roughness",    "mode": "L",    "alpha": False},
    "displacement": {"suffix": "Displacement", "mode": "L",    "alpha": False},
    "scattering":   {"suffix": "Scattering",   "mode": "L",    "alpha": False},
    "opacity":      {"suffix": "Opacity",      "mode": "L",    "alpha": False},
}


def find_connected_components(mask, w, h):
    """Flood-fill connected component labeling on a binary mask."""
    visited = bytearray(w * h)
    components = []

    for start_y in range(h):
        for start_x in range(w):
            idx = start_y * w + start_x
            if visited[idx] or not mask[idx]:
                continue

            # BFS flood fill
            min_x, min_y = start_x, start_y
            max_x, max_y = start_x, start_y
            queue = deque()
            queue.append((start_x, start_y))
            visited[idx] = 1
            pixel_count = 0

            while queue:
                x, y = queue.popleft()
                pixel_count += 1
                min_x = min(min_x, x)
                max_x = max(max_x, x)
                min_y = min(min_y, y)
                max_y = max(max_y, y)

                for nx, ny in ((x-1, y), (x+1, y), (x, y-1), (x, y+1)):
                    if 0 <= nx < w and 0 <= ny < h:
                        nidx = ny * w + nx
                        if not visited[nidx] and mask[nidx]:
                            visited[nidx] = 1
                            queue.append((nx, ny))

            components.append((min_x, min_y, max_x, max_y, pixel_count))

    return components


def main():
    tex_dir = Path(__file__).resolve().parent.parent / "assets" / "textures"
    out_dir = tex_dir / "leaves"
    out_dir.mkdir(exist_ok=True)

    # Load opacity map and find leaf bounding boxes
    opacity_path = tex_dir / "LeafSet023_1K-JPG_Opacity.jpg"
    opacity_img = Image.open(opacity_path).convert("L")
    w, h = opacity_img.size
    opacity_data = opacity_img.load()

    mask = bytearray(w * h)
    for y in range(h):
        for x in range(w):
            if opacity_data[x, y] > OPACITY_THRESHOLD:
                mask[y * w + x] = 1

    components = find_connected_components(mask, w, h)

    min_area = w * h * 0.01
    leaves = [(x0, y0, x1, y1) for x0, y0, x1, y1, area in components if area >= min_area]
    leaves.sort(key=lambda b: (b[1], b[0]))

    print(f"Found {len(leaves)} leaves in {opacity_path.name}")

    # Compute padded crop boxes once
    crop_boxes = []
    for x0, y0, x1, y1 in leaves:
        px0 = max(0, x0 - PADDING)
        py0 = max(0, y0 - PADDING)
        px1 = min(w - 1, x1 + PADDING)
        py1 = min(h - 1, y1 + PADDING)
        crop_boxes.append((px0, py0, px1 + 1, py1 + 1))

    # Load and split each PBR map
    for map_name, info in PBR_MAPS.items():
        src_path = tex_dir / f"LeafSet023_1K-JPG_{info['suffix']}.jpg"
        if not src_path.exists():
            print(f"  WARNING: {src_path.name} not found, skipping")
            continue

        src_img = Image.open(src_path).convert(info["mode"])
        if src_img.size != (w, h):
            print(f"  WARNING: {src_path.name} size {src_img.size} != opacity {w}x{h}, skipping")
            continue

        for i, crop_box in enumerate(crop_boxes):
            crop = src_img.crop(crop_box)

            if info["alpha"]:
                opacity_crop = opacity_img.crop(crop_box)
                crop = crop.copy()
                crop.putalpha(opacity_crop)

            out_path = out_dir / f"leaf_{i:02d}_{map_name}.png"
            crop.save(out_path)

        print(f"  {info['suffix']:14s} -> {len(crop_boxes)} leaves")

    # Print summary
    print()
    for i, (box, (x0, y0, x1, y1)) in enumerate(zip(crop_boxes, leaves)):
        cw = box[2] - box[0]
        ch = box[3] - box[1]
        print(f"  leaf_{i:02d}  {cw}x{ch}  bbox=({x0},{y0})-({x1},{y1})")

    print(f"\nSaved to {out_dir}")


if __name__ == "__main__":
    main()

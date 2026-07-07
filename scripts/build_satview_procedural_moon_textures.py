#!/usr/bin/env python3
"""Build deterministic 2k presentation textures for incompletely mapped moons.

These are deliberately labelled procedural: they are plausible visual stand-ins,
not reconstructed surface maps. Real USGS/PDS mosaics are bundled separately when
global equirectangular coverage is suitable for direct sphere mapping.
"""

from __future__ import annotations

import math
import random
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "assets" / "satview" / "textures"
WIDTH = 2048
HEIGHT = 1024


MOONS = {
    "deimos": ((112, 101, 89), 1201, 34, 0.45),
    "mimas": ((166, 166, 160), 1202, 82, 0.80),
    "tethys": ((185, 187, 185), 1203, 56, 0.60),
    "dione": ((174, 177, 178), 1204, 48, 0.58),
    "rhea": ((158, 158, 155), 1205, 70, 0.72),
    "miranda": ((153, 154, 151), 1206, 46, 0.65),
    "ariel": ((170, 174, 174), 1207, 45, 0.55),
    "umbriel": ((81, 80, 79), 1208, 31, 0.42),
    "titania": ((139, 139, 137), 1209, 43, 0.55),
    "oberon": ((116, 106, 101), 1210, 47, 0.58),
    "triton": ((172, 151, 146), 1211, 38, 0.42),
}


def clamp(value: float) -> int:
    return max(0, min(255, round(value)))


def make_texture(
    name: str,
    base: tuple[int, int, int],
    seed: int,
    crater_count: int,
    contrast: float,
) -> Image.Image:
    rng = random.Random(seed)
    low_width, low_height = 512, 256
    pixels: list[tuple[int, int, int]] = []
    phases = [rng.random() * math.tau for _ in range(8)]
    for y in range(low_height):
        latitude = (y / (low_height - 1) - 0.5) * math.pi
        for x in range(low_width):
            longitude = x / low_width * math.tau
            terrain = (
                math.sin(longitude * 2.0 + phases[0]) * 0.35
                + math.sin(longitude * 5.0 + latitude * 1.7 + phases[1]) * 0.20
                + math.sin(longitude * 11.0 - latitude * 4.0 + phases[2]) * 0.10
                + math.sin(latitude * 7.0 + phases[3]) * 0.18
                + math.sin(longitude * 23.0 + latitude * 13.0 + phases[4]) * 0.05
            )
            polar = -0.10 * abs(math.sin(latitude))
            value = (terrain + polar) * 42.0 * contrast
            pixels.append(tuple(clamp(channel + value) for channel in base))

    image = Image.new("RGB", (low_width, low_height))
    image.putdata(pixels)
    image = image.resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS)
    image = image.filter(ImageFilter.GaussianBlur(1.2))

    overlay = Image.new("RGBA", image.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(overlay, "RGBA")
    for crater_index in range(crater_count):
        radius = rng.expovariate(1.0 / 20.0) + 4.0
        if name == "mimas" and crater_index == 0:
            radius = 116.0
            x, y = int(WIDTH * 0.58), int(HEIGHT * 0.43)
        else:
            x, y = rng.randrange(WIDTH), rng.randrange(HEIGHT)
        aspect = max(0.25, math.cos((y / HEIGHT - 0.5) * math.pi))
        rx, ry = radius / max(aspect, 0.32), radius
        shade = rng.randint(18, 40)
        for wrapped_x in (x - WIDTH, x, x + WIDTH):
            box = (wrapped_x - rx, y - ry, wrapped_x + rx, y + ry)
            draw.ellipse(box, fill=(8, 8, 8, shade))
            inner = (
                wrapped_x - rx * 0.72,
                y - ry * 0.72,
                wrapped_x + rx * 0.72,
                y + ry * 0.72,
            )
            draw.ellipse(inner, fill=(150, 150, 145, shade // 3))
            draw.arc(
                box,
                start=190,
                end=325,
                fill=(240, 240, 232, shade + 18),
                width=max(1, int(radius * 0.10)),
            )

    if name in {"tethys", "dione", "ariel", "miranda"}:
        for _ in range(10 if name == "miranda" else 5):
            y = rng.randrange(80, HEIGHT - 80)
            amplitude = rng.randrange(18, 70)
            points = [
                (x, y + math.sin(x / WIDTH * math.tau * rng.randint(1, 4) + phases[5]) * amplitude)
                for x in range(-64, WIDTH + 65, 32)
            ]
            draw.line(points, fill=(220, 225, 225, 70), width=rng.randrange(3, 10))

    if name == "triton":
        for _ in range(28):
            x = rng.randrange(WIDTH)
            y = rng.randrange(HEIGHT)
            width = rng.randrange(12, 60)
            draw.line((x, y, x + width, y + rng.randrange(30, 130)), fill=(80, 65, 60, 65), width=2)

    overlay = overlay.filter(ImageFilter.GaussianBlur(0.8))
    return Image.alpha_composite(image.convert("RGBA"), overlay).convert("RGB")


def main() -> None:
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for name, (base, seed, craters, contrast) in MOONS.items():
        path = OUTPUT / f"{name}_procedural_2k.jpg"
        make_texture(name, base, seed, craters, contrast).save(
            path, "JPEG", quality=91, optimize=True, subsampling=0
        )
        print(path.relative_to(ROOT))


if __name__ == "__main__":
    main()

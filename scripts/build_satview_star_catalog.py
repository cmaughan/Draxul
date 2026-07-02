#!/usr/bin/env python3
"""Build SatView's generic binary star catalog from Hipparcos data."""

from __future__ import annotations

import argparse
import math
import struct
import sys
import urllib.parse
import urllib.request
from pathlib import Path


VIZIER_URL = "https://vizier.cds.unistra.fr/viz-bin/asu-tsv"
SOURCE_NAME = "I/239/hip_main"
MAGIC = b"DXSTAR1\0"
VERSION = 1
HEADER_SIZE = 32
RECORD_SIZE = 32
SOURCE_ID_HIPPARCOS = 1


def clamp(value: float, minimum: float, maximum: float) -> float:
    return max(minimum, min(maximum, value))


def parse_float(value: str) -> float | None:
    value = value.strip()
    if not value:
        return None
    try:
        parsed = float(value)
    except ValueError:
        return None
    return parsed if math.isfinite(parsed) else None


def fetch_hipparcos_tsv(timeout: float) -> str:
    query = urllib.parse.urlencode(
        [
            ("-source", SOURCE_NAME),
            ("-out", "HIP"),
            ("-out", "RAICRS"),
            ("-out", "DEICRS"),
            ("-out", "Vmag"),
            ("-out", "B-V"),
            ("-out", "SpType"),
            ("-out.max", "120000"),
        ]
    )
    request = urllib.request.Request(
        f"{VIZIER_URL}?{query}",
        headers={"User-Agent": "Draxul SatView star catalog builder"},
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return response.read().decode("utf-8", errors="replace")


def iter_vizier_rows(tsv: str):
    lines = tsv.splitlines()
    header_index = next(
        (index for index, line in enumerate(lines) if line.startswith("HIP\t")),
        None,
    )
    if header_index is None:
        raise ValueError("VizieR TSV header not found")

    headers = lines[header_index].split("\t")
    for line in lines[header_index + 3 :]:
        if not line.strip() or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) < len(headers):
            parts.extend([""] * (len(headers) - len(parts)))
        yield dict(zip(headers, parts))


def bv_to_kelvin(bv: float) -> float:
    bv = clamp(bv, -0.4, 2.0)
    return clamp(
        4600.0 * (1.0 / (0.92 * bv + 1.7) + 1.0 / (0.92 * bv + 0.62)),
        2500.0,
        12000.0,
    )


def kelvin_to_rgb(kelvin: float) -> tuple[float, float, float]:
    temp = kelvin / 100.0
    if temp <= 66.0:
        red = 255.0
        green = 99.4708025861 * math.log(max(temp, 1.0)) - 161.1195681661
        blue = 0.0 if temp <= 19.0 else 138.5177312231 * math.log(temp - 10.0) - 305.0447927307
    else:
        red = 329.698727446 * ((temp - 60.0) ** -0.1332047592)
        green = 288.1221695283 * ((temp - 60.0) ** -0.0755148492)
        blue = 255.0
    return (
        clamp(red, 0.0, 255.0) / 255.0,
        clamp(green, 0.0, 255.0) / 255.0,
        clamp(blue, 0.0, 255.0) / 255.0,
    )


def spectral_rgb(spectral_type: str) -> tuple[float, float, float]:
    code = spectral_type.strip().upper()[:1]
    return {
        "O": (0.68, 0.78, 1.00),
        "B": (0.76, 0.84, 1.00),
        "A": (0.92, 0.96, 1.00),
        "F": (1.00, 0.98, 0.88),
        "G": (1.00, 0.90, 0.72),
        "K": (1.00, 0.72, 0.48),
        "M": (1.00, 0.52, 0.40),
    }.get(code, (1.0, 1.0, 1.0))


def saturate(rgb: tuple[float, float, float], amount: float) -> tuple[float, float, float]:
    luminance = rgb[0] * 0.2126 + rgb[1] * 0.7152 + rgb[2] * 0.0722
    return tuple(clamp(luminance + (channel - luminance) * amount, 0.0, 1.0) for channel in rgb)


def star_color(row: dict[str, str], saturation: float) -> tuple[float, float, float]:
    bv = parse_float(row.get("B-V", ""))
    if bv is not None:
        rgb = kelvin_to_rgb(bv_to_kelvin(bv))
    else:
        rgb = spectral_rgb(row.get("SpType", ""))
    return saturate(rgb, saturation)


def visual_weight(magnitude: float) -> tuple[float, float]:
    flux = 10.0 ** (-0.4 * magnitude)
    brightness = clamp(flux ** 0.42, 0.008, 1.0)
    size = 0.00075 + 0.0052 * (flux ** 0.20)
    if magnitude < 1.0:
        size += (1.0 - magnitude) * 0.0009
    return brightness, clamp(size, 0.0008, 0.0090)


def render_direction_from_icrs(ra_degrees: float, dec_degrees: float) -> tuple[float, float, float]:
    ra = math.radians(ra_degrees)
    dec = math.radians(dec_degrees)
    cos_dec = math.cos(dec)
    equatorial = (
        cos_dec * math.cos(ra),
        cos_dec * math.sin(ra),
        math.sin(dec),
    )
    return (-equatorial[1], equatorial[2], -equatorial[0])


def build_records(tsv: str, maximum_stars: int, saturation: float):
    records = []
    for row in iter_vizier_rows(tsv):
        hip = parse_float(row.get("HIP", ""))
        ra = parse_float(row.get("RAICRS", ""))
        dec = parse_float(row.get("DEICRS", ""))
        magnitude = parse_float(row.get("Vmag", ""))
        if hip is None or ra is None or dec is None or magnitude is None:
            continue

        direction = render_direction_from_icrs(ra, dec)
        rgb = star_color(row, saturation)
        brightness, size = visual_weight(magnitude)
        records.append(
            (
                magnitude,
                int(hip),
                direction,
                (
                    rgb[0] * brightness,
                    rgb[1] * brightness,
                    rgb[2] * brightness,
                    size,
                ),
            )
        )

    records.sort(key=lambda item: (item[0], item[1]))
    return records[:maximum_stars]


def write_catalog(path: Path, records) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as output:
        output.write(
            struct.pack(
                "<8sIIIIII",
                MAGIC,
                VERSION,
                HEADER_SIZE,
                RECORD_SIZE,
                len(records),
                0,
                SOURCE_ID_HIPPARCOS,
            )
        )
        for magnitude, _hip, direction, color_size in records:
            output.write(
                struct.pack(
                    "<ffffffff",
                    direction[0],
                    direction[1],
                    direction[2],
                    magnitude,
                    color_size[0],
                    color_size[1],
                    color_size[2],
                    color_size[3],
                )
            )


def main(argv: list[str]) -> int:
    repo_root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input",
        type=Path,
        help="Use a previously downloaded VizieR ASU TSV instead of fetching it.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=repo_root / "assets" / "satview" / "catalog" / "stars.dxstar",
        help="Output binary catalog path.",
    )
    parser.add_argument("--max-stars", type=int, default=100000)
    parser.add_argument("--saturation", type=float, default=1.35)
    parser.add_argument("--timeout", type=float, default=60.0)
    args = parser.parse_args(argv)

    maximum_stars = max(0, args.max_stars)
    if args.input:
        tsv = args.input.read_text(encoding="utf-8", errors="replace")
    else:
        tsv = fetch_hipparcos_tsv(args.timeout)

    records = build_records(tsv, maximum_stars, args.saturation)
    write_catalog(args.output, records)
    print(f"Wrote {len(records)} Hipparcos stars to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

#!/usr/bin/env python3
"""Build SatView's binary constellation-line catalog from CC BY 4.0 data."""

from __future__ import annotations

import argparse
import csv
import io
import math
import struct
import sys
import urllib.parse
import urllib.request
from pathlib import Path


CONSTELLATION_LINES_URL = (
    "https://raw.githubusercontent.com/MarcvdSluys/ConstellationLines/"
    "v1.3/ConstellationLines.csv"
)
BRIGHT_STAR_SOURCE = "V/50/catalog"
HIPPARCOS_SOURCE = "I/239/hip_main"
VIZIER_URL = "https://vizier.cds.unistra.fr/viz-bin/asu-tsv"
MAGIC = b"DXCLINE1"
VERSION = 1
HEADER_SIZE = 32
RECORD_SIZE = 24
SOURCE_ID_CONSTELLATION_LINES_V1_3 = 1


def fetch_text(url: str, timeout: float) -> str:
    request = urllib.request.Request(
        url,
        headers={"User-Agent": "Draxul SatView constellation catalog builder"},
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return response.read().decode("utf-8", errors="replace")


def fetch_bright_star_tsv(timeout: float) -> str:
    query = urllib.parse.urlencode(
        [
            ("-source", BRIGHT_STAR_SOURCE),
            ("-out", "HR"),
            ("-out", "HD"),
            ("-out", "RAJ2000"),
            ("-out", "DEJ2000"),
            ("-out.max", "10000"),
        ]
    )
    return fetch_text(f"{VIZIER_URL}?{query}", timeout)


def fetch_hipparcos_tsv(timeout: float) -> str:
    query = urllib.parse.urlencode(
        [
            ("-source", HIPPARCOS_SOURCE),
            ("-out", "HIP"),
            ("-out", "HD"),
            ("-out", "RAICRS"),
            ("-out", "DEICRS"),
            ("-out", "Vmag"),
            ("-out.max", "120000"),
        ]
    )
    return fetch_text(f"{VIZIER_URL}?{query}", timeout)


def parse_constellation_segments(text: str) -> tuple[list[tuple[int, int]], int]:
    reader = csv.DictReader(io.StringIO(text))
    segments: list[tuple[int, int]] = []
    seen: set[tuple[int, int]] = set()
    constellations: set[str] = set()
    for source_row in reader:
        row = {
            (key or "").strip(): (value or "").strip()
            for key, value in source_row.items()
        }
        abbreviation = row.get("abr", "")
        if not abbreviation:
            continue
        constellations.add(abbreviation)
        count = int(row["nr"])
        stars = [int(row[f"s{index:02d}"]) for index in range(1, count + 1)]
        for start, end in zip(stars, stars[1:]):
            if start == end:
                continue
            key = (min(start, end), max(start, end))
            if key in seen:
                continue
            seen.add(key)
            segments.append((start, end))

    if len(constellations) != 88:
        raise ValueError(f"expected 88 constellations, found {len(constellations)}")
    return segments, len(constellations)


def parse_angle(value: str, *, hours: bool) -> float:
    parts = value.strip().split()
    if len(parts) != 3:
        raise ValueError(f"invalid catalog angle: {value!r}")
    sign = -1.0 if parts[0].startswith("-") else 1.0
    primary = abs(float(parts[0]))
    result = primary + float(parts[1]) / 60.0 + float(parts[2]) / 3600.0
    if hours:
        return result * 15.0
    return sign * result


def parse_bright_star_positions(text: str) -> dict[int, tuple[float, float, int | None]]:
    lines = text.splitlines()
    header_index = next(
        (index for index, line in enumerate(lines) if line.startswith("HR\t")),
        None,
    )
    if header_index is None:
        raise ValueError("VizieR Bright Star Catalogue header not found")

    headers = [header.strip() for header in lines[header_index].split("\t")]
    positions: dict[int, tuple[float, float, int | None]] = {}
    for line in lines[header_index + 3 :]:
        if not line.strip() or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) < len(headers):
            parts.extend([""] * (len(headers) - len(parts)))
        row = {key: value.strip() for key, value in zip(headers, parts)}
        if not row.get("HR") or not row.get("RAJ2000") or not row.get("DEJ2000"):
            continue
        positions[int(row["HR"])] = (
            parse_angle(row["RAJ2000"], hours=True),
            parse_angle(row["DEJ2000"], hours=False),
            int(row["HD"]) if row.get("HD") else None,
        )
    return positions


def parse_hipparcos_positions(text: str) -> dict[int, tuple[float, float]]:
    lines = text.splitlines()
    header_index = next(
        (index for index, line in enumerate(lines) if line.startswith("HIP\t")),
        None,
    )
    if header_index is None:
        raise ValueError("VizieR Hipparcos header not found")

    headers = [header.strip() for header in lines[header_index].split("\t")]
    candidates: dict[int, tuple[float, float, float]] = {}
    for line in lines[header_index + 3 :]:
        if not line.strip() or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) < len(headers):
            parts.extend([""] * (len(headers) - len(parts)))
        row = {key: value.strip() for key, value in zip(headers, parts)}
        if not row.get("HD") or not row.get("RAICRS") or not row.get("DEICRS"):
            continue
        hd = int(row["HD"])
        magnitude = float(row["Vmag"]) if row.get("Vmag") else math.inf
        candidate = (magnitude, float(row["RAICRS"]), float(row["DEICRS"]))
        if hd not in candidates or candidate[0] < candidates[hd][0]:
            candidates[hd] = candidate
    return {hd: (value[1], value[2]) for hd, value in candidates.items()}


def render_direction(ra_degrees: float, dec_degrees: float) -> tuple[float, float, float]:
    ra = math.radians(ra_degrees)
    dec = math.radians(dec_degrees)
    cos_dec = math.cos(dec)
    equatorial = (
        cos_dec * math.cos(ra),
        cos_dec * math.sin(ra),
        math.sin(dec),
    )
    return (-equatorial[1], equatorial[2], -equatorial[0])


def build_records(
    segments: list[tuple[int, int]],
    positions: dict[int, tuple[float, float, int | None]],
    hipparcos_positions: dict[int, tuple[float, float]],
) -> tuple[list[tuple[tuple[float, float, float], tuple[float, float, float]]], int]:
    missing = sorted({star for segment in segments for star in segment if star not in positions})
    if missing:
        preview = ", ".join(str(star) for star in missing[:12])
        raise ValueError(f"Bright Star Catalogue positions missing for HR ids: {preview}")

    crossmatched_ids: set[int] = set()

    def direction(hr: int) -> tuple[float, float, float]:
        ra, dec, hd = positions[hr]
        if hd is not None and hd in hipparcos_positions:
            ra, dec = hipparcos_positions[hd]
            crossmatched_ids.add(hr)
        return render_direction(ra, dec)

    return [(direction(start), direction(end)) for start, end in segments], len(crossmatched_ids)


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
                SOURCE_ID_CONSTELLATION_LINES_V1_3,
            )
        )
        for start, end in records:
            output.write(struct.pack("<ffffff", *start, *end))


def main(argv: list[str]) -> int:
    repo_root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--lines-input",
        type=Path,
        help="Use a downloaded ConstellationLines CSV instead of fetching v1.3.",
    )
    parser.add_argument(
        "--stars-input",
        type=Path,
        help="Use a downloaded VizieR Bright Star Catalogue TSV instead of fetching it.",
    )
    parser.add_argument(
        "--hipparcos-input",
        type=Path,
        help="Use a downloaded VizieR Hipparcos TSV instead of fetching it.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=repo_root / "assets" / "satview" / "catalog" / "constellations.dxline",
    )
    parser.add_argument("--timeout", type=float, default=60.0)
    args = parser.parse_args(argv)

    lines_text = (
        args.lines_input.read_text(encoding="utf-8", errors="replace")
        if args.lines_input
        else fetch_text(CONSTELLATION_LINES_URL, args.timeout)
    )
    stars_text = (
        args.stars_input.read_text(encoding="utf-8", errors="replace")
        if args.stars_input
        else fetch_bright_star_tsv(args.timeout)
    )
    hipparcos_text = (
        args.hipparcos_input.read_text(encoding="utf-8", errors="replace")
        if args.hipparcos_input
        else fetch_hipparcos_tsv(args.timeout)
    )
    segments, constellation_count = parse_constellation_segments(lines_text)
    records, crossmatched_count = build_records(
        segments,
        parse_bright_star_positions(stars_text),
        parse_hipparcos_positions(hipparcos_text),
    )
    write_catalog(args.output, records)
    print(
        f"Wrote {len(records)} segments for {constellation_count} constellations "
        f"({crossmatched_count} unique HR endpoints matched to Hipparcos) "
        f"to {args.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

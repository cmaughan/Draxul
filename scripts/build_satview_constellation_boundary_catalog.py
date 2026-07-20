#!/usr/bin/env python3
"""Build SatView's binary J2000 constellation boundary and label catalog."""

from __future__ import annotations

import argparse
import json
import math
import struct
import sys
import urllib.request
from pathlib import Path
from typing import Any

import satview_catalog_container as catalog_container


D3_CELESTIAL_COMMIT = "7e720a3de062059d4c5400a379146a601d9010e0"
D3_CELESTIAL_RAW = f"https://raw.githubusercontent.com/ofrohn/d3-celestial/{D3_CELESTIAL_COMMIT}/data"
BORDERS_URL = f"{D3_CELESTIAL_RAW}/constellations.borders.json"
NAMES_URL = f"{D3_CELESTIAL_RAW}/constellations.json"
MAGIC = b"DXCBND01"
VERSION = 1
HEADER_SIZE = 56
SEGMENT_RECORD_SIZE = 32
LABEL_RECORD_SIZE = 32
SOURCE_ID_D3_CELESTIAL = 1
MAX_SEGMENT_DEGREES = 1.0


def fetch_json(url: str, timeout: float) -> dict[str, Any]:
    request = urllib.request.Request(
        url,
        headers={"User-Agent": "Draxul SatView constellation boundary builder"},
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return json.loads(response.read().decode("utf-8"))


def load_json(path: Path | None, url: str, timeout: float) -> dict[str, Any]:
    if path is not None:
        return json.loads(path.read_text(encoding="utf-8"))
    return fetch_json(url, timeout)


def require_feature_collection(data: dict[str, Any], name: str) -> list[dict[str, Any]]:
    if data.get("type") != "FeatureCollection" or not isinstance(data.get("features"), list):
        raise ValueError(f"{name} is not a GeoJSON FeatureCollection")
    return data["features"]


def render_direction(longitude_degrees: float, declination_degrees: float) -> tuple[float, float, float]:
    right_ascension_degrees = longitude_degrees % 360.0
    ra = math.radians(right_ascension_degrees)
    dec = math.radians(declination_degrees)
    cos_dec = math.cos(dec)
    equatorial = (
        cos_dec * math.cos(ra),
        cos_dec * math.sin(ra),
        math.sin(dec),
    )
    return (-equatorial[1], equatorial[2], -equatorial[0])


def slerp(start: tuple[float, float, float], end: tuple[float, float, float], t: float) -> tuple[float, float, float]:
    dot = max(-1.0, min(1.0, sum(a * b for a, b in zip(start, end))))
    angle = math.acos(dot)
    if angle < 1.0e-8:
        return start
    sin_angle = math.sin(angle)
    a = math.sin((1.0 - t) * angle) / sin_angle
    b = math.sin(t * angle) / sin_angle
    result = tuple(a * lhs + b * rhs for lhs, rhs in zip(start, end))
    length = math.sqrt(sum(value * value for value in result))
    return tuple(value / length for value in result)


def parse_coordinate(value: Any, context: str) -> tuple[float, float]:
    if not isinstance(value, list) or len(value) < 2:
        raise ValueError(f"{context} is not a longitude/declination coordinate")
    longitude = float(value[0])
    declination = float(value[1])
    if not math.isfinite(longitude) or not math.isfinite(declination):
        raise ValueError(f"{context} contains a non-finite coordinate")
    if declination < -90.0 or declination > 90.0:
        raise ValueError(f"{context} declination is outside [-90, 90]")
    return longitude, declination


def build_segments(data: dict[str, Any]) -> list[tuple[tuple[float, float, float], tuple[float, float, float], int]]:
    features = require_feature_collection(data, "constellation borders")
    if len(features) != 257:
        raise ValueError(f"expected 257 shared border features, found {len(features)}")

    records: list[tuple[tuple[float, float, float], tuple[float, float, float], int]] = []
    maximum_angle = math.radians(MAX_SEGMENT_DEGREES)
    for feature_index, feature in enumerate(features):
        geometry = feature.get("geometry", {})
        if geometry.get("type") != "MultiLineString":
            raise ValueError(f"border feature {feature_index} is not a MultiLineString")
        ids = feature.get("ids")
        if not isinstance(ids, str) or not ids:
            raise ValueError(f"border feature {feature_index} has no constellation ids")
        for line_index, line in enumerate(geometry.get("coordinates", [])):
            if not isinstance(line, list) or len(line) < 2:
                raise ValueError(f"border feature {feature_index} line {line_index} is too short")
            directions = [
                render_direction(*parse_coordinate(coordinate, f"border {feature_index}:{line_index}"))
                for coordinate in line
            ]
            for start, end in zip(directions, directions[1:]):
                dot = max(-1.0, min(1.0, sum(a * b for a, b in zip(start, end))))
                angle = math.acos(dot)
                subdivisions = max(1, math.ceil(angle / maximum_angle))
                previous = start
                for subdivision in range(1, subdivisions + 1):
                    current = slerp(start, end, subdivision / subdivisions)
                    records.append((previous, current, feature_index))
                    previous = current
    if not records:
        raise ValueError("constellation border catalog produced no segments")
    return records


def build_labels(data: dict[str, Any]) -> list[tuple[tuple[float, float, float], int, str, str, int]]:
    features = require_feature_collection(data, "constellation names")
    if len(features) != 89:
        raise ValueError(f"expected 89 constellation areas, found {len(features)}")

    records: list[tuple[tuple[float, float, float], int, str, str, int]] = []
    designations: set[str] = set()
    for area_index, feature in enumerate(features):
        geometry = feature.get("geometry", {})
        properties = feature.get("properties", {})
        if geometry.get("type") != "Point":
            raise ValueError(f"constellation label {area_index} is not a Point")
        longitude, declination = parse_coordinate(
            geometry.get("coordinates"), f"constellation label {area_index}"
        )
        name = properties.get("name")
        designation = properties.get("desig")
        rank_text = properties.get("rank")
        if not isinstance(name, str) or not name:
            raise ValueError(f"constellation label {area_index} has no name")
        if not isinstance(designation, str) or len(designation) != 3:
            raise ValueError(f"constellation label {area_index} has an invalid designation")
        rank = int(rank_text)
        if rank not in (1, 2, 3):
            raise ValueError(f"constellation label {area_index} has invalid rank {rank}")
        designations.add(designation)
        records.append((render_direction(longitude, declination), rank, designation, name, area_index))

    if len(designations) != 88:
        raise ValueError(f"expected 88 unique constellation designations, found {len(designations)}")
    serpens = [record for record in records if record[2] == "Ser"]
    if sorted(record[3] for record in serpens) != ["Serpens Caput", "Serpens Cauda"]:
        raise ValueError("expected separate Serpens Caput and Serpens Cauda label areas")
    return records


def write_catalog(path: Path, segments, labels) -> None:
    strings = bytearray()
    label_records = []
    for direction, rank, designation, name, area_index in labels:
        encoded_name = name.encode("utf-8")
        name_offset = len(strings)
        strings.extend(encoded_name)
        label_records.append(
            (direction, rank, name_offset, len(encoded_name), designation.encode("ascii"), area_index)
        )
    if len(strings) > catalog_container.MAX_STRING_BYTES:
        raise ValueError(
            f"label strings are {len(strings)} bytes, "
            f"limit is {catalog_container.MAX_STRING_BYTES}"
        )

    segment_payload = catalog_container.pack_records(
        (
            struct.pack("<ffffffII", *start, *end, feature_index, 0)
            for start, end, feature_index in segments
        ),
        SEGMENT_RECORD_SIZE,
        "segment",
    )
    label_payload = catalog_container.pack_records(
        (
            struct.pack(
                "<fffIII4sI",
                *direction,
                rank,
                name_offset,
                name_size,
                designation.ljust(4, b"\0"),
                area_index,
            )
            for direction, rank, name_offset, name_size, designation, area_index in label_records
        ),
        LABEL_RECORD_SIZE,
        "label",
    )

    segment_offset = HEADER_SIZE
    label_offset = segment_offset + len(segment_payload)
    string_offset = label_offset + len(label_payload)
    header = catalog_container.pack_header(
        MAGIC,
        VERSION,
        HEADER_SIZE,
        [
            SEGMENT_RECORD_SIZE,
            LABEL_RECORD_SIZE,
            len(segments),
            len(label_records),
            segment_offset,
            label_offset,
            string_offset,
            len(strings),
            SOURCE_ID_D3_CELESTIAL,
            0,
        ],
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as output:
        output.write(header)
        output.write(segment_payload)
        output.write(label_payload)
        output.write(strings)


def main(argv: list[str]) -> int:
    repo_root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--borders-input", type=Path)
    parser.add_argument("--names-input", type=Path)
    parser.add_argument(
        "--output",
        type=Path,
        default=repo_root / "assets" / "satview" / "catalog" / "constellation_boundaries.dxbnd",
    )
    parser.add_argument("--timeout", type=float, default=60.0)
    args = parser.parse_args(argv)

    borders = load_json(args.borders_input, BORDERS_URL, args.timeout)
    names = load_json(args.names_input, NAMES_URL, args.timeout)
    segments = build_segments(borders)
    labels = build_labels(names)
    write_catalog(args.output, segments, labels)
    print(
        f"Wrote {len(segments)} boundary segments and {len(labels)} label areas "
        f"to {args.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

#!/usr/bin/env python3
"""Build SatView's lunar surface object catalogue from LROC and GCAT data."""

from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import math
import re
import struct
import sys
import urllib.request
import zipfile
from collections import defaultdict
from dataclasses import dataclass
from datetime import date, datetime
from pathlib import Path


LROC_URL = (
    "https://pds.lroc.im-ldi.com/data/LRO-L-LROC-5-RDR-V1.0/"
    "LROLRC_2001/EXTRAS/SHAPEFILE/ANTHROPOGENIC_OBJECTS/"
    "ANTHROPOGENIC_OBJECTS_180.ZIP"
)
GCAT_URL = "https://planet4589.org/space/gcat/tsv/cat/landercat.tsv"
LROC_PRODUCT_URL = (
    "https://data.lroc.im-ldi.com/lroc/view_rdr/"
    "SHAPEFILE_ANTHROPOGENIC_OBJECTS"
)
GCAT_DOCUMENTATION_URL = "https://planet4589.org/space/gcat/root.html"
LROC_SHA256 = "5f2c9ca336aff3738d7da5ed881fe1eefaeee2c55e78f706fa49d59223138ad5"
GCAT_SHA256 = "a75cb4f5b7ae3b2968692d7f5f1f0d4162a656430eca4d1f01cb170f24de516d"
LROC_VERSION = "2025-09-04"
GCAT_VERSION = "2026-07-03"
SITE_CLUSTER_DISTANCE_KM = 15.0
MOON_RADIUS_KM = 1737.4

OUTPUT_COLUMNS = (
    "ID",
    "MISSION_ID",
    "MISSION_NAME",
    "PARENT_SITE_ID",
    "DISPLAY_NAME",
    "VEHICLE_NAME",
    "KIND",
    "SOURCE_KIND",
    "STATUS",
    "LATITUDE_DEGREES",
    "LONGITUDE_EAST_DEGREES",
    "COORDINATE_UNCERTAINTY_M",
    "RADIAL_DISTANCE_M",
    "ARRIVAL_DATE",
    "LOCATION_QUALITY",
    "COORDINATE_SOURCE",
    "RADIUS_SOURCE",
    "COSPAR_ID",
    "GCAT_ID",
    "OWNER",
    "COUNTRY",
    "REFERENCES",
    "DISPLAY_RANK",
    "SITE_REPRESENTATIVE",
    "CREWED_MISSION",
)


@dataclass
class GcatRecord:
    jcat_id: str
    cospar_id: str
    name: str
    owner: str
    country: str
    longitude: float | None
    latitude: float | None
    landing_date: date | None
    comment: str


def fetch_bytes(url: str, timeout: float) -> bytes:
    request = urllib.request.Request(
        url,
        headers={"User-Agent": "Draxul SatView lunar catalogue builder"},
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return response.read()


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def verify_source(name: str, data: bytes, expected: str, allow_source_update: bool) -> None:
    actual = sha256(data)
    if actual == expected:
        return
    message = f"{name} SHA-256 changed: expected {expected}, received {actual}"
    if not allow_source_update:
        raise ValueError(message + "; review the new source and pass --allow-source-update")
    print(f"WARNING: {message}", file=sys.stderr)


def parse_dbf(data: bytes) -> list[dict[str, str]]:
    if len(data) < 33:
        raise ValueError("LROC DBF header is truncated")
    record_count = struct.unpack_from("<I", data, 4)[0]
    header_length = struct.unpack_from("<H", data, 8)[0]
    record_length = struct.unpack_from("<H", data, 10)[0]
    if header_length > len(data) or record_length == 0:
        raise ValueError("LROC DBF header is invalid")

    fields: list[tuple[str, int]] = []
    offset = 32
    while offset + 32 <= header_length and data[offset] != 0x0D:
        descriptor = data[offset : offset + 32]
        name = descriptor[:11].split(b"\0", 1)[0].decode("ascii").strip()
        fields.append((name, descriptor[16]))
        offset += 32

    rows: list[dict[str, str]] = []
    for index in range(record_count):
        start = header_length + index * record_length
        record = data[start : start + record_length]
        if len(record) != record_length:
            raise ValueError("LROC DBF record is truncated")
        if record[0:1] == b"*":
            continue
        row: dict[str, str] = {}
        field_offset = 1
        for name, length in fields:
            value = record[field_offset : field_offset + length]
            row[name] = value.decode("latin-1", errors="replace").strip()
            field_offset += length
        rows.append(row)
    return rows


def load_lroc_rows(archive: bytes) -> list[dict[str, str]]:
    with zipfile.ZipFile(io.BytesIO(archive)) as zipped:
        dbf_names = [name for name in zipped.namelist() if name.upper().endswith("_180.DBF")]
        if len(dbf_names) != 1:
            raise ValueError(f"expected one -180 LROC DBF, found {len(dbf_names)}")
        return parse_dbf(zipped.read(dbf_names[0]))


def optional_float(value: str) -> float | None:
    text = value.strip()
    if not text or text == "-":
        return None
    try:
        result = float(text)
    except ValueError:
        return None
    return result if math.isfinite(result) else None


def parse_gcat_date(value: str) -> date | None:
    digits = re.sub(r"\D", "", value)
    if len(digits) == 8:
        try:
            return datetime.strptime(digits, "%Y%m%d").date()
        except ValueError:
            return None
    match = re.match(r"\s*(\d{4})\s+([A-Za-z]{3})\s+(\d{1,2})", value)
    if not match:
        return None
    try:
        return datetime.strptime(" ".join(match.groups()), "%Y %b %d").date()
    except ValueError:
        return None


def load_gcat_rows(data: bytes) -> list[GcatRecord]:
    text = data.decode("utf-8", errors="replace")
    lines = text.splitlines()
    if not lines or not lines[0].startswith("#JCAT\t"):
        raise ValueError("GCAT lander catalogue header is missing")
    reader = csv.DictReader([lines[0][1:], *[line for line in lines[1:] if not line.startswith("#")]], delimiter="\t")
    rows: list[GcatRecord] = []
    for raw in reader:
        row = {(key or "").strip(): (value or "").strip() for key, value in raw.items()}
        if row.get("World") != "Luna":
            continue
        rows.append(
            GcatRecord(
                jcat_id=row.get("JCAT", ""),
                cospar_id=row.get("Piece", ""),
                name=row.get("Name", ""),
                owner=row.get("Owner", ""),
                country=row.get("State", ""),
                longitude=optional_float(row.get("Lon", "")),
                latitude=optional_float(row.get("Lat", "")),
                landing_date=parse_gcat_date(row.get("LandDate", "")),
                comment=row.get("Comment", ""),
            )
        )
    return rows


def normalized_key(value: str) -> str:
    replacements = {
        "’": "",
        "'": "",
        "\u2011": "-",
        "\u2013": "-",
        "\u2014": "-",
    }
    for source, target in replacements.items():
        value = value.replace(source, target)
    return "".join(character.lower() for character in value if character.isalnum())


def slug(value: str) -> str:
    text = re.sub(r"[^a-z0-9]+", "-", value.lower()).strip("-")
    return text or "object"


def normalize_longitude(longitude: float) -> float:
    result = math.remainder(longitude, 360.0)
    return 180.0 if result == -180.0 else result


def great_circle_distance_km(a: dict[str, object], b: dict[str, object]) -> float:
    lat_a = math.radians(float(a["LATITUDE_DEGREES"]))
    lat_b = math.radians(float(b["LATITUDE_DEGREES"]))
    lon_delta = math.radians(
        normalize_longitude(float(a["LONGITUDE_EAST_DEGREES"]) - float(b["LONGITUDE_EAST_DEGREES"]))
    )
    cosine = (
        math.sin(lat_a) * math.sin(lat_b)
        + math.cos(lat_a) * math.cos(lat_b) * math.cos(lon_delta)
    )
    return MOON_RADIUS_KM * math.acos(max(-1.0, min(1.0, cosine)))


def normalize_kind(source_kind: str) -> str:
    value = source_kind.lower()
    if "roving vehicle" in value or "rover" in value:
        return "rover"
    if "retroreflector" in value:
        return "retroreflector"
    if any(token in value for token in ("experiment", "alsep", "seismic", "instrument")):
        return "deployed_instrument"
    if "s-ivb" in value or "booster" in value or "retrorocket" in value:
        return "rocket_stage"
    if "impact" in value:
        return "impact_site"
    if "descent stage" in value:
        return "crewed_artifact"
    if "lander" in value:
        return "lander"
    return "unknown"


def status_for_kind(kind: str) -> str:
    if kind in {"impact_site", "rocket_stage"}:
        return "impacted"
    if kind in {"deployed_instrument", "retroreflector", "crewed_artifact"}:
        return "deployed"
    return "landed"


def kind_rank(kind: str) -> int:
    return {
        "lander": 0,
        "crewed_artifact": 1,
        "rover": 2,
        "retroreflector": 3,
        "deployed_instrument": 4,
        "impact_site": 5,
        "rocket_stage": 6,
        "unknown": 7,
    }.get(kind, 8)


def match_gcat(row: dict[str, str], records: list[GcatRecord]) -> GcatRecord | None:
    mission = normalized_key(row.get("MISSION", ""))
    names = {
        normalized_key(row.get("SHORT_NAME", "")),
        normalized_key(row.get("VEHCL_NAME", "")),
    }
    names.discard("")
    arrival = parse_gcat_date(row.get("DATE", ""))
    lroc_latitude = optional_float(row.get("LATITUDE", ""))
    lroc_longitude = optional_float(row.get("LONGITUDE", ""))
    best: tuple[int, float, GcatRecord] | None = None
    for candidate in records:
        candidate_name = normalized_key(candidate.name)
        candidate_text = normalized_key(candidate.name + " " + candidate.comment)
        mission_match = mission and (mission in candidate_text or candidate_name in mission)
        name_match = any(name in candidate_text or candidate_name in name for name in names)
        date_match = arrival is not None and candidate.landing_date == arrival
        score = 4 * int(name_match) + 3 * int(mission_match) + 2 * int(date_match)
        distance = math.inf
        if (
            lroc_latitude is not None
            and lroc_longitude is not None
            and candidate.latitude is not None
            and candidate.longitude is not None
        ):
            delta_latitude = lroc_latitude - candidate.latitude
            delta_longitude = normalize_longitude(lroc_longitude - candidate.longitude)
            distance = math.hypot(delta_latitude, delta_longitude)
            if distance < 1.0:
                score += 2
        if not mission_match and not name_match and not (date_match and distance < 5.0):
            continue
        choice = (score, -distance, candidate)
        if best is None or choice[:2] > best[:2]:
            best = choice
    return best[2] if best and best[0] >= 4 else None


def build_records(lroc_rows: list[dict[str, str]], gcat_rows: list[GcatRecord]) -> tuple[list[dict[str, object]], list[dict[str, object]]]:
    records: list[dict[str, object]] = []
    conflicts: list[dict[str, object]] = []
    used_ids: defaultdict[str, int] = defaultdict(int)

    for row in lroc_rows:
        latitude = optional_float(row.get("LATITUDE", ""))
        longitude = optional_float(row.get("LONGITUDE", ""))
        if latitude is None or longitude is None:
            raise ValueError(f"LROC feature {row.get('SHORT_NAME', '')!r} has no coordinate")
        if not -90.0 <= latitude <= 90.0:
            raise ValueError(f"invalid latitude {latitude} for {row.get('SHORT_NAME', '')}")
        longitude = normalize_longitude(longitude)
        mission_name = row.get("MISSION", "").strip()
        mission_id = slug(mission_name)
        display_name = row.get("SHORT_NAME", "").strip() or row.get("VEHCL_NAME", "").strip()
        base_id = slug(f"{mission_name}-{display_name}")
        used_ids[base_id] += 1
        object_id = base_id if used_ids[base_id] == 1 else f"{base_id}-{used_ids[base_id]}"
        source_kind = row.get("OBJECT", "").strip()
        kind = normalize_kind(source_kind)
        arrival = parse_gcat_date(row.get("DATE", ""))
        gcat = match_gcat(row, gcat_rows)
        references = [LROC_PRODUCT_URL]
        if row.get("REFERENCES", "").strip():
            references.append(row["REFERENCES"].strip())
        if gcat:
            references.append(GCAT_DOCUMENTATION_URL)
            if gcat.latitude is not None and gcat.longitude is not None:
                delta_latitude = abs(latitude - gcat.latitude)
                delta_longitude = abs(normalize_longitude(longitude - gcat.longitude))
                if max(delta_latitude, delta_longitude) >= 0.05:
                    conflicts.append(
                        {
                            "id": object_id,
                            "lroc": {"latitude": latitude, "longitude_east": longitude},
                            "gcat": {
                                "latitude": gcat.latitude,
                                "longitude_east": normalize_longitude(gcat.longitude),
                            },
                            "resolution": "LROC coordinate retained",
                        }
                    )

        records.append(
            {
                "ID": object_id,
                "MISSION_ID": mission_id,
                "MISSION_NAME": mission_name,
                "PARENT_SITE_ID": "",
                "DISPLAY_NAME": display_name,
                "VEHICLE_NAME": row.get("VEHCL_NAME", "").strip(),
                "KIND": kind,
                "SOURCE_KIND": source_kind,
                "STATUS": status_for_kind(kind),
                "LATITUDE_DEGREES": latitude,
                "LONGITUDE_EAST_DEGREES": longitude,
                "COORDINATE_UNCERTAINTY_M": optional_float(row.get("UNCERTAIN", "")),
                "RADIAL_DISTANCE_M": optional_float(row.get("RADIUS", "")),
                "ARRIVAL_DATE": arrival.isoformat() if arrival else row.get("DATE", "").strip(),
                "LOCATION_QUALITY": "confirmed",
                "COORDINATE_SOURCE": row.get("COORD_SRC", "").strip(),
                "RADIUS_SOURCE": row.get("RAD_SRC", "").strip(),
                "COSPAR_ID": gcat.cospar_id if gcat else "",
                "GCAT_ID": gcat.jcat_id if gcat else "",
                "OWNER": gcat.owner if gcat else "",
                "COUNTRY": gcat.country if gcat else "",
                "REFERENCES": " | ".join(dict.fromkeys(references)),
                "DISPLAY_RANK": kind_rank(kind),
                "SITE_REPRESENTATIVE": 0,
                "CREWED_MISSION": int(mission_name.startswith("Apollo ")),
            }
        )

    by_mission: defaultdict[str, list[dict[str, object]]] = defaultdict(list)
    for record in records:
        by_mission[str(record["MISSION_ID"])].append(record)
    for mission_id, mission_records in by_mission.items():
        parent = list(range(len(mission_records)))

        def find(index: int) -> int:
            while parent[index] != index:
                parent[index] = parent[parent[index]]
                index = parent[index]
            return index

        def join(left: int, right: int) -> None:
            root_left = find(left)
            root_right = find(right)
            if root_left != root_right:
                parent[root_right] = root_left

        for left in range(len(mission_records)):
            for right in range(left + 1, len(mission_records)):
                if great_circle_distance_km(mission_records[left], mission_records[right]) <= SITE_CLUSTER_DISTANCE_KM:
                    join(left, right)
        clusters: defaultdict[int, list[dict[str, object]]] = defaultdict(list)
        for index, record in enumerate(mission_records):
            clusters[find(index)].append(record)
        ordered_clusters = sorted(
            clusters.values(),
            key=lambda cluster: min(str(record["ID"]) for record in cluster),
        )
        for site_index, cluster in enumerate(ordered_clusters, start=1):
            site_id = f"{mission_id}-site-{site_index:02d}"
            representative = min(
                cluster,
                key=lambda record: (int(record["DISPLAY_RANK"]), str(record["ID"])),
            )
            representative["SITE_REPRESENTATIVE"] = 1
            for record in cluster:
                record["PARENT_SITE_ID"] = site_id

    records.sort(
        key=lambda record: (
            str(record["ARRIVAL_DATE"]),
            str(record["MISSION_NAME"]),
            str(record["PARENT_SITE_ID"]),
            int(record["DISPLAY_RANK"]),
            str(record["ID"]),
        )
    )
    return records, conflicts


def csv_value(value: object) -> object:
    if isinstance(value, float):
        return format(value, ".10g")
    return "" if value is None else value


def write_catalog(path: Path, records: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=OUTPUT_COLUMNS, lineterminator="\n")
        writer.writeheader()
        for record in records:
            writer.writerow({column: csv_value(record.get(column, "")) for column in OUTPUT_COLUMNS})


def write_manifest(
    path: Path,
    records: list[dict[str, object]],
    conflicts: list[dict[str, object]],
    lroc_data: bytes,
    gcat_data: bytes,
) -> None:
    manifest = {
        "schema_version": 1,
        "source_snapshot_date": GCAT_VERSION,
        "record_count": len(records),
        "mission_count": len({record["MISSION_ID"] for record in records}),
        "site_count": len({record["PARENT_SITE_ID"] for record in records}),
        "sources": [
            {
                "name": "LROC Anthropogenic Features on the Moon",
                "version": LROC_VERSION,
                "url": LROC_URL,
                "sha256": sha256(lroc_data),
                "role": "coordinate authority",
            },
            {
                "name": "GCAT Lander Catalogue",
                "version": GCAT_VERSION,
                "url": GCAT_URL,
                "sha256": sha256(gcat_data),
                "license": "CC-BY-4.0",
                "role": "identity and ownership enrichment",
            },
        ],
        "coordinate_conflicts": conflicts,
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main(argv: list[str]) -> int:
    repo_root = Path(__file__).resolve().parents[1]
    output_dir = repo_root / "assets" / "satview" / "catalog"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lroc-input", type=Path, help="Use a downloaded LROC ZIP.")
    parser.add_argument("--gcat-input", type=Path, help="Use a downloaded GCAT TSV.")
    parser.add_argument("--output", type=Path, default=output_dir / "lunar_surface_objects.csv")
    parser.add_argument("--manifest", type=Path, default=output_dir / "lunar_surface_sources.json")
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--allow-source-update", action="store_true")
    args = parser.parse_args(argv)

    lroc_data = args.lroc_input.read_bytes() if args.lroc_input else fetch_bytes(LROC_URL, args.timeout)
    gcat_data = args.gcat_input.read_bytes() if args.gcat_input else fetch_bytes(GCAT_URL, args.timeout)
    verify_source("LROC archive", lroc_data, LROC_SHA256, args.allow_source_update)
    verify_source("GCAT catalogue", gcat_data, GCAT_SHA256, args.allow_source_update)

    records, conflicts = build_records(load_lroc_rows(lroc_data), load_gcat_rows(gcat_data))
    if len(records) != 70:
        raise ValueError(f"expected 70 reviewed LROC features, found {len(records)}")
    write_catalog(args.output, records)
    write_manifest(args.manifest, records, conflicts, lroc_data, gcat_data)
    print(
        f"Wrote {len(records)} objects in "
        f"{len({record['PARENT_SITE_ID'] for record in records})} sites to {args.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

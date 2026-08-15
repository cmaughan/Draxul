#!/usr/bin/env python3
"""Build SatView's curated Moon-relative spacecraft ephemeris from JPL Horizons."""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import io
import json
import math
import os
import sys
import urllib.parse
import urllib.request
import xml.etree.ElementTree as et
from pathlib import Path


HORIZONS_URL = "https://ssd.jpl.nasa.gov/api/horizons.api"
SSC_URL = "https://sscweb.gsfc.nasa.gov/WS/sscr/2"
UNIX_EPOCH_JULIAN_DATE = 2440587.5
SECONDS_PER_DAY = 86400.0
FRAME_NAME = "MOON_ICRF"
CSV_COLUMNS = (
    "NORAD_CAT_ID",
    "SOURCE",
    "FRAME",
    "TRACK_HORIZON_MINUTES",
    "UNIX_SECONDS",
    "X_KM",
    "Y_KM",
    "Z_KM",
    "VX_KM_PER_S",
    "VY_KM_PER_S",
    "VZ_KM_PER_S",
)


def parse_date(value: str) -> dt.date:
    try:
        return dt.date.fromisoformat(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid ISO date: {value}") from exc


def fetch_horizons_vectors(
    horizons_id: str,
    start: dt.date,
    stop: dt.date,
    step_minutes: int,
    timeout: float,
    center: str = "500@301",
) -> str:
    parameters = {
        "format": "json",
        "COMMAND": f"'{horizons_id}'",
        "OBJ_DATA": "'NO'",
        "MAKE_EPHEM": "'YES'",
        "EPHEM_TYPE": "'VECTORS'",
        "CENTER": f"'{center}'",
        "START_TIME": f"'{start.isoformat()}'",
        "STOP_TIME": f"'{stop.isoformat()}'",
        "STEP_SIZE": f"'{step_minutes} min'",
        "REF_PLANE": "'FRAME'",
        "OUT_UNITS": "'KM-S'",
        "VEC_TABLE": "'2'",
        "VEC_CORR": "'NONE'",
        "CSV_FORMAT": "'YES'",
        "TIME_TYPE": "'UT'",
    }
    url = f"{HORIZONS_URL}?{urllib.parse.urlencode(parameters)}"
    request = urllib.request.Request(
        url,
        headers={"User-Agent": "Draxul SatView lunar ephemeris builder"},
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        payload = json.loads(response.read().decode("utf-8"))
    result = payload.get("result", "")
    if "$$SOE" not in result or "$$EOE" not in result:
        diagnostic = " ".join(line.strip() for line in result.splitlines() if line.strip())
        raise RuntimeError(diagnostic[:600] or "Horizons returned no vector table")
    return result


def fetch_ssc_gei_positions(
    ssc_id: str,
    start: dt.date,
    stop: dt.date,
    step_minutes: int,
    timeout: float,
) -> list[tuple[float, float, float, float]]:
    if step_minutes < 1:
        raise RuntimeError("NASA SSC step must be at least one minute")
    start_text = start.strftime("%Y%m%dT000000Z")
    stop_text = stop.strftime("%Y%m%dT000000Z")
    url = (
        f"{SSC_URL}/locations/{urllib.parse.quote(ssc_id)}/"
        f"{start_text},{stop_text}/geij2000/?resolutionFactor={step_minutes}"
    )
    request = urllib.request.Request(
        url,
        headers={
            "Accept": "application/xml",
            "User-Agent": "Draxul SatView lunar ephemeris builder",
        },
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        root = et.fromstring(response.read())
    namespace = {"ssc": "http://sscweb.gsfc.nasa.gov/schema"}
    data = root.find(".//ssc:Data", namespace)
    if data is None:
        raise RuntimeError(f"NASA SSC returned no data for {ssc_id}")
    times = [
        dt.datetime.fromisoformat(node.text.replace("Z", "+00:00")).timestamp()
        for node in data.findall("ssc:Time", namespace)
        if node.text
    ]
    coordinates = data.find("ssc:Coordinates", namespace)
    if coordinates is None:
        raise RuntimeError(f"NASA SSC returned no coordinates for {ssc_id}")
    axes = [
        [float(node.text) for node in coordinates.findall(f"ssc:{axis}", namespace)]
        for axis in ("X", "Y", "Z")
    ]
    if not times or any(len(axis) != len(times) for axis in axes):
        raise RuntimeError(f"NASA SSC returned inconsistent coordinates for {ssc_id}")
    return [
        (time, axes[0][i], axes[1][i], axes[2][i])
        for i, time in enumerate(times)
    ]


def parse_vector_table(result: str) -> list[tuple[float, ...]]:
    table = result.split("$$SOE", 1)[1].split("$$EOE", 1)[0]
    samples: list[tuple[float, ...]] = []
    for row in csv.reader(io.StringIO(table)):
        fields = [field.strip() for field in row]
        if len(fields) < 8 or not fields[0]:
            continue
        try:
            jd_ut = float(fields[0])
            state = tuple(float(value) for value in fields[2:8])
        except ValueError as exc:
            raise RuntimeError(f"invalid Horizons vector row: {row!r}") from exc
        values = (jd_ut, *state)
        if not all(math.isfinite(value) for value in values):
            raise RuntimeError("Horizons returned a non-finite vector")
        unix_seconds = (jd_ut - UNIX_EPOCH_JULIAN_DATE) * SECONDS_PER_DAY
        samples.append((unix_seconds, *state))
    if len(samples) < 2:
        raise RuntimeError("Horizons returned fewer than two vector samples")
    if any(b[0] <= a[0] for a, b in zip(samples, samples[1:])):
        raise RuntimeError("Horizons vector times are not strictly increasing")
    return samples


def finite_difference_states(
    positions: list[tuple[float, float, float, float]],
) -> list[tuple[float, ...]]:
    if len(positions) < 2:
        raise RuntimeError("fewer than two positions available for velocity derivation")
    states: list[tuple[float, ...]] = []
    for i, (time, x, y, z) in enumerate(positions):
        first = max(0, i - 1)
        second = min(len(positions) - 1, i + 1)
        span = positions[second][0] - positions[first][0]
        if span <= 0.0:
            raise RuntimeError("position times are not strictly increasing")
        vx = (positions[second][1] - positions[first][1]) / span
        vy = (positions[second][2] - positions[first][2]) / span
        vz = (positions[second][3] - positions[first][3]) / span
        states.append((time, x, y, z, vx, vy, vz))
    return states


def fetch_ssc_moon_relative_states(
    ssc_id: str,
    start: dt.date,
    stop: dt.date,
    step_minutes: int,
    timeout: float,
) -> list[tuple[float, ...]]:
    spacecraft = fetch_ssc_gei_positions(ssc_id, start, stop, step_minutes, timeout)
    moon = parse_vector_table(
        fetch_horizons_vectors("301", start, stop, step_minutes, timeout, "500@399")
    )
    moon_by_second = {round(sample[0]): sample for sample in moon}
    relative: list[tuple[float, float, float, float]] = []
    for time, x, y, z in spacecraft:
        moon_state = moon_by_second.get(round(time))
        if moon_state is None:
            raise RuntimeError(f"no matching JPL Moon vector for NASA SSC time {time}")
        relative.append(
            (time, x - moon_state[1], y - moon_state[2], z - moon_state[3])
        )
    return finite_difference_states(relative)


def load_targets(path: Path) -> list[dict[str, object]]:
    document = json.loads(path.read_text(encoding="utf-8"))
    if document.get("schema_version") != 1:
        raise RuntimeError("unsupported lunar ephemeris target schema")
    targets = document.get("targets")
    if not isinstance(targets, list) or not targets:
        raise RuntimeError("lunar ephemeris target list is empty")
    return targets


def write_catalog(path: Path, rows: list[tuple[object, ...]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    try:
        with temporary.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.writer(stream, lineterminator="\n")
            writer.writerow(CSV_COLUMNS)
            writer.writerows(rows)
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def main() -> int:
    repository_root = Path(__file__).resolve().parents[1]
    default_targets = repository_root / "plugins/satview/assets/catalog/lunar_ephemeris_targets.json"
    default_output = repository_root / "plugins/satview/assets/catalog/lunar_ephemeris.csv"
    today = dt.datetime.now(dt.timezone.utc).date()

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--targets", type=Path, default=default_targets)
    parser.add_argument("--output", type=Path, default=default_output)
    # Four days of history keeps CAPSTONE's seven-day track complete at the
    # current instant while leaving interpolation margin at the boundary.
    parser.add_argument("--start", type=parse_date, default=today - dt.timedelta(days=4))
    parser.add_argument("--stop", type=parse_date, default=today + dt.timedelta(days=14))
    parser.add_argument("--timeout", type=float, default=60.0)
    args = parser.parse_args()

    if args.stop <= args.start:
        parser.error("--stop must be later than --start")

    rows: list[tuple[object, ...]] = []
    used_targets = 0
    for target in load_targets(args.targets):
        name = str(target["name"])
        norad_id = int(target["norad_catalog_id"])
        provider = str(target.get("provider", "horizons"))
        step_minutes = int(target["step_minutes"])
        track_horizon_minutes = float(target["track_horizon_minutes"])
        stop = args.stop
        if coverage_end := target.get("coverage_end"):
            stop = min(stop, parse_date(str(coverage_end)))
        if stop <= args.start:
            print(f"skip {name}: requested window starts after known coverage", file=sys.stderr)
            continue

        if provider == "horizons":
            horizons_id = str(target["horizons_id"])
            print(
                f"fetch {name}: Horizons {horizons_id}, {args.start} to {stop}, "
                f"{step_minutes} min",
                file=sys.stderr,
            )
            samples = parse_vector_table(fetch_horizons_vectors(
                horizons_id,
                args.start,
                stop,
                step_minutes,
                args.timeout,
            ))
            source = f"NASA/JPL Horizons {horizons_id}"
        elif provider == "nasa_ssc":
            ssc_id = str(target["ssc_id"])
            print(
                f"fetch {name}: NASA SSC {ssc_id}, {args.start} to {stop}, "
                f"{step_minutes} min",
                file=sys.stderr,
            )
            samples = fetch_ssc_moon_relative_states(
                ssc_id,
                args.start,
                stop,
                step_minutes,
                args.timeout,
            )
            source = f"NASA SSC prediction {ssc_id} + JPL Moon"
        else:
            raise RuntimeError(f"unsupported ephemeris provider {provider!r} for {name}")
        for unix_seconds, x, y, z, vx, vy, vz in samples:
            rows.append(
                (
                    norad_id,
                    source,
                    FRAME_NAME,
                    f"{track_horizon_minutes:.3f}",
                    f"{unix_seconds:.3f}",
                    f"{x:.15g}",
                    f"{y:.15g}",
                    f"{z:.15g}",
                    f"{vx:.15g}",
                    f"{vy:.15g}",
                    f"{vz:.15g}",
                )
            )
        used_targets += 1
        print(f"  {len(samples)} samples", file=sys.stderr)

    if used_targets == 0:
        raise RuntimeError("no targets had coverage in the requested interval")
    write_catalog(args.output, rows)
    print(f"wrote {len(rows)} samples for {used_targets} targets to {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, KeyError, TypeError, ValueError, RuntimeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)

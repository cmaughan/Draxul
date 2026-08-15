"""Tests for the shared SatView catalog container writer and generators.

Covers three areas:
* golden framing of the shipped catalog assets (byte-exact headers),
* validation errors in the product-owned tools/satview_catalog_container.py,
* generator determinism: each build script run twice on small synthetic
  inputs must produce byte-identical output (the real inputs are network
  downloads, so determinism is verified on synthetic data offline).
"""

from __future__ import annotations

import json
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SATVIEW_ROOT = Path(__file__).resolve().parents[1]
SCRIPTS_DIR = SATVIEW_ROOT / "tools"
CATALOG_DIR = SATVIEW_ROOT / "assets" / "catalog"

sys.path.insert(0, str(SCRIPTS_DIR))

import satview_catalog_container as container  # noqa: E402

STAR_MAGIC = b"DXSTAR1\0"
LINE_MAGIC = b"DXCLINE1"
BOUNDARY_MAGIC = b"DXCBND01"

# The same golden bytes as tests/satview_catalog_container_tests.cpp:
# a two-record DXSTAR1 catalog, spelled out byte for byte.
GOLDEN_STAR_CATALOG_HEX = (
    "4458535441523100"  # magic "DXSTAR1\0"
    "01000000"          # version 1
    "20000000"          # header_size 32
    "20000000"          # record_size 32
    "02000000"          # record_count 2
    "00000000"          # flags 0
    "01000000"          # source_id 1
    # record 0: direction (1, 0, 0), magnitude -1.5, color (0.5, 0.25, 1), size 0.125
    "0000803f" "00000000" "00000000" "0000c0bf"
    "0000003f" "0000803e" "0000803f" "0000003e"
    # record 1: direction (0, 1, 0), magnitude 2.5, color (1, 1, 1), size 0.25
    "00000000" "0000803f" "00000000" "00002040"
    "0000803f" "0000803f" "0000803f" "0000803e"
)

SHIPPED_STAR_HEADER_HEX = (
    "4458535441523100" "01000000" "20000000" "20000000" "a0860100" "00000000" "01000000"
)
SHIPPED_LINE_HEADER_HEX = (
    "4458434c494e4531" "01000000" "20000000" "18000000" "90020000" "00000000" "01000000"
)


class PackHelpersTests(unittest.TestCase):
    def test_single_table_header_matches_shipped_star_header(self) -> None:
        header = container.pack_single_table_header(
            STAR_MAGIC, 1, 32, 100000, source_id=1)
        self.assertEqual(header, bytes.fromhex(SHIPPED_STAR_HEADER_HEX))

    def test_single_table_header_matches_shipped_line_header(self) -> None:
        header = container.pack_single_table_header(
            LINE_MAGIC, 1, 24, 656, source_id=1)
        self.assertEqual(header, bytes.fromhex(SHIPPED_LINE_HEADER_HEX))

    def test_writer_reproduces_the_golden_star_catalog_bytes(self) -> None:
        records = [
            struct.pack("<8f", 1.0, 0.0, 0.0, -1.5, 0.5, 0.25, 1.0, 0.125),
            struct.pack("<8f", 0.0, 1.0, 0.0, 2.5, 1.0, 1.0, 1.0, 0.25),
        ]
        header = container.pack_single_table_header(STAR_MAGIC, 1, 32, 2, source_id=1)
        payload = container.pack_records(records, 32, "star")
        self.assertEqual(header + payload, bytes.fromhex(GOLDEN_STAR_CATALOG_HEX))

    def test_magic_must_be_exactly_eight_bytes(self) -> None:
        with self.assertRaises(ValueError):
            container.pack_prologue(b"SHORT", 1, 32)
        with self.assertRaises(ValueError):
            container.pack_prologue(b"TOO-LONG-MAGIC", 1, 32)

    def test_header_size_must_match_packed_size(self) -> None:
        with self.assertRaises(ValueError):
            container.pack_header(BOUNDARY_MAGIC, 1, 56, [0] * 3)

    def test_record_count_limit_is_enforced(self) -> None:
        with self.assertRaises(ValueError):
            container.pack_single_table_header(
                STAR_MAGIC, 1, 32, container.MAX_RECORD_COUNT + 1)

    def test_record_size_mismatch_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            container.pack_records([b"\0" * 8], 32, "star")


class ValidateHelperTests(unittest.TestCase):
    @staticmethod
    def catalog(magic=STAR_MAGIC, version=1, header_size=32, record_size=32,
                record_count=1, payload=b"\0" * 32) -> bytes:
        header = struct.pack(
            "<8sIIIIII", magic, version, header_size, record_size, record_count, 0, 1)
        return header + payload

    def test_valid_catalog_passes(self) -> None:
        count = container.validate_single_table_catalog(
            self.catalog(), magic=STAR_MAGIC, version=1, record_size=32)
        self.assertEqual(count, 1)

    def test_bad_magic_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            container.validate_single_table_catalog(
                self.catalog(magic=b"BADMAGIC"), magic=STAR_MAGIC, version=1, record_size=32)

    def test_bad_version_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            container.validate_single_table_catalog(
                self.catalog(version=2), magic=STAR_MAGIC, version=1, record_size=32)

    def test_small_header_size_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            container.validate_single_table_catalog(
                self.catalog(header_size=16), magic=STAR_MAGIC, version=1, record_size=32)

    def test_wrong_record_size_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            container.validate_single_table_catalog(
                self.catalog(record_size=24), magic=STAR_MAGIC, version=1, record_size=32)

    def test_count_overflow_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            container.validate_single_table_catalog(
                self.catalog(record_count=container.MAX_RECORD_COUNT + 1),
                magic=STAR_MAGIC, version=1, record_size=32)

    def test_truncated_payload_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            container.validate_single_table_catalog(
                self.catalog(record_count=2), magic=STAR_MAGIC, version=1, record_size=32)


class ShippedAssetTests(unittest.TestCase):
    """Golden-load every shipped catalog asset and pin its framing."""

    def test_shipped_star_catalog_framing(self) -> None:
        data = (CATALOG_DIR / "stars.dxstar").read_bytes()
        self.assertEqual(data[:32], bytes.fromhex(SHIPPED_STAR_HEADER_HEX))
        count = container.validate_single_table_catalog(
            data, magic=STAR_MAGIC, version=1, record_size=32)
        self.assertEqual(count, 100000)
        self.assertEqual(len(data), 32 + 100000 * 32)

    def test_shipped_line_catalog_framing(self) -> None:
        data = (CATALOG_DIR / "constellations.dxline").read_bytes()
        self.assertEqual(data[:32], bytes.fromhex(SHIPPED_LINE_HEADER_HEX))
        count = container.validate_single_table_catalog(
            data, magic=LINE_MAGIC, version=1, record_size=24)
        self.assertEqual(count, 656)
        self.assertEqual(len(data), 32 + 656 * 24)

    def test_shipped_boundary_catalog_framing(self) -> None:
        data = (CATALOG_DIR / "constellation_boundaries.dxbnd").read_bytes()
        header = struct.unpack("<8s12I", data[:56])
        self.assertEqual(
            header,
            (BOUNDARY_MAGIC, 1, 56, 32, 32, 4936, 89, 56, 158008, 160856, 681, 1, 0),
        )
        self.assertEqual(len(data), 160856 + 681)


def synthetic_star_tsv() -> str:
    lines = [
        "# synthetic VizieR extract",
        "HIP\tRAICRS\tDEICRS\tVmag\tB-V\tSpType",
        "\tdeg\tdeg\tmag\tmag\t",
        "---\t---\t---\t---\t---\t---",
        "1\t10.0\t20.0\t1.5\t0.5\tG2V",
        "2\t100.0\t-45.0\t-1.0\t\tA0",
        "3\t200.0\t60.0\t3.25\t1.2\tK0",
        "4\t355.5\t-89.5\t6.0\t0.0\tF5",
        "bad\tx\ty\tz\t\t",
    ]
    return "\n".join(lines) + "\n"


def synthetic_constellation_lines_csv() -> str:
    rows = ["abr,nr,s01,s02,s03"]
    for index in range(88):
        rows.append(f"C{index:02d},2,{2 * index + 1},{2 * index + 2},")
    return "\n".join(rows) + "\n"


def synthetic_bright_star_tsv() -> str:
    lines = [
        "# synthetic Bright Star Catalogue extract",
        "HR\tHD\tRAJ2000\tDEJ2000",
        "\t\t\"h:m:s\"\t\"d:m:s\"",
        "---\t---\t---\t---",
    ]
    for hr in range(1, 177):
        hd = str(1000 + hr) if hr % 2 == 0 else ""
        ra = f"{hr % 24:02d} {(hr * 7) % 60:02d} 30.0"
        dec_sign = "-" if hr % 3 == 0 else "+"
        dec = f"{dec_sign}{hr % 89:02d} 15 00"
        lines.append(f"{hr}\t{hd}\t{ra}\t{dec}")
    return "\n".join(lines) + "\n"


def synthetic_hipparcos_tsv() -> str:
    lines = [
        "# synthetic Hipparcos extract",
        "HIP\tHD\tRAICRS\tDEICRS\tVmag",
        "\t\tdeg\tdeg\tmag",
        "---\t---\t---\t---\t---",
    ]
    for hr in range(2, 177, 2):
        ra = (hr * 1.9) % 360.0
        dec = ((hr * 0.9) % 178.0) - 89.0
        vmag = 1.0 + (hr % 7) * 0.5
        lines.append(f"{90000 + hr}\t{1000 + hr}\t{ra:.4f}\t{dec:.4f}\t{vmag:.2f}")
    return "\n".join(lines) + "\n"


def synthetic_borders_json() -> str:
    features = []
    for index in range(257):
        lon = (index * 1.37) % 360.0
        dec = ((index * 0.61) % 160.0) - 80.0
        features.append(
            {
                "type": "Feature",
                "ids": f"B{index:03d}",
                "geometry": {
                    "type": "MultiLineString",
                    "coordinates": [[[lon, dec], [lon + 0.5, dec + 0.25]]],
                },
            }
        )
    return json.dumps({"type": "FeatureCollection", "features": features})


def synthetic_names_json() -> str:
    designations = [f"X{index:02d}" for index in range(87)] + ["Ser", "Ser"]
    names = [f"Constellation {index:02d}" for index in range(87)]
    names += ["Serpens Caput", "Serpens Cauda"]
    features = []
    for index, (designation, name) in enumerate(zip(designations, names)):
        lon = (index * 4.04) % 360.0
        dec = ((index * 1.97) % 170.0) - 85.0
        features.append(
            {
                "type": "Feature",
                "properties": {"name": name, "desig": designation, "rank": str(index % 3 + 1)},
                "geometry": {"type": "Point", "coordinates": [lon, dec]},
            }
        )
    return json.dumps({"type": "FeatureCollection", "features": features})


def run_generator(script: str, arguments: list[str]) -> None:
    result = subprocess.run(
        [sys.executable, str(SCRIPTS_DIR / script), *arguments],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        raise AssertionError(
            f"{script} failed with code {result.returncode}:\n{result.stdout}\n{result.stderr}")


class GeneratorDeterminismTests(unittest.TestCase):
    """Each generator, run twice on the same input, must emit identical bytes.

    The real inputs are network downloads (VizieR, GitHub); these tests use
    small synthetic inputs so they run offline.
    """

    def test_star_catalog_generator_is_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            work = Path(raw)
            tsv = work / "stars.tsv"
            tsv.write_text(synthetic_star_tsv(), encoding="utf-8")
            outputs = [work / "first.dxstar", work / "second.dxstar"]
            for output in outputs:
                run_generator(
                    "build_satview_star_catalog.py",
                    ["--input", str(tsv), "--output", str(output)],
                )
            first, second = (output.read_bytes() for output in outputs)
            self.assertEqual(first, second)
            count = container.validate_single_table_catalog(
                first, magic=STAR_MAGIC, version=1, record_size=32)
            self.assertEqual(count, 4)

    def test_constellation_line_generator_is_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            work = Path(raw)
            lines_csv = work / "lines.csv"
            stars_tsv = work / "bright.tsv"
            hipparcos_tsv = work / "hipparcos.tsv"
            lines_csv.write_text(synthetic_constellation_lines_csv(), encoding="utf-8")
            stars_tsv.write_text(synthetic_bright_star_tsv(), encoding="utf-8")
            hipparcos_tsv.write_text(synthetic_hipparcos_tsv(), encoding="utf-8")
            outputs = [work / "first.dxline", work / "second.dxline"]
            for output in outputs:
                run_generator(
                    "build_satview_constellation_catalog.py",
                    [
                        "--lines-input", str(lines_csv),
                        "--stars-input", str(stars_tsv),
                        "--hipparcos-input", str(hipparcos_tsv),
                        "--output", str(output),
                    ],
                )
            first, second = (output.read_bytes() for output in outputs)
            self.assertEqual(first, second)
            count = container.validate_single_table_catalog(
                first, magic=LINE_MAGIC, version=1, record_size=24)
            self.assertEqual(count, 88)

    def test_boundary_generator_is_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            work = Path(raw)
            borders = work / "borders.json"
            names = work / "names.json"
            borders.write_text(synthetic_borders_json(), encoding="utf-8")
            names.write_text(synthetic_names_json(), encoding="utf-8")
            outputs = [work / "first.dxbnd", work / "second.dxbnd"]
            for output in outputs:
                run_generator(
                    "build_satview_constellation_boundary_catalog.py",
                    [
                        "--borders-input", str(borders),
                        "--names-input", str(names),
                        "--output", str(output),
                    ],
                )
            first, second = (output.read_bytes() for output in outputs)
            self.assertEqual(first, second)
            header = struct.unpack("<8s12I", first[:56])
            self.assertEqual(header[0], BOUNDARY_MAGIC)
            self.assertEqual(header[5], 257)  # segment_count
            self.assertEqual(header[6], 89)  # label_count
            # string_offset + string_size == file size
            self.assertEqual(header[9] + header[10], len(first))


if __name__ == "__main__":
    unittest.main()

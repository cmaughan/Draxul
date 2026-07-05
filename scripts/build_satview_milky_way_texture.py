#!/usr/bin/env python3
"""Build SatView's 4K celestial Milky Way texture from the NASA SVS EXR."""

from __future__ import annotations

import argparse
import hashlib
import shutil
import subprocess
import tempfile
import urllib.request
from pathlib import Path


SOURCE_URL = (
    "https://svs.gsfc.nasa.gov/vis/a000000/a004800/a004851/"
    "milkyway_2020_4k.exr"
)
SOURCE_SHA256 = "2eb802d6e68d170b410f766c7fec07f7518619f6b6708fdc81e9302d93e74fdb"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).resolve().parents[1]
        / "assets"
        / "satview"
        / "textures"
        / "milky_way_nasa_4k.jpg",
    )
    args = parser.parse_args()

    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        parser.error("ffmpeg is required to convert the source OpenEXR image")

    with tempfile.TemporaryDirectory(prefix="draxul-milky-way-") as temporary:
        source = Path(temporary) / "milkyway_2020_4k.exr"
        print(f"Downloading {SOURCE_URL}")
        urllib.request.urlretrieve(SOURCE_URL, source)
        source_hash = sha256(source)
        if source_hash != SOURCE_SHA256:
            raise RuntimeError(
                f"source checksum mismatch: expected {SOURCE_SHA256}, got {source_hash}"
            )

        args.output.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            [
                ffmpeg,
                "-y",
                "-hide_banner",
                "-loglevel",
                "warning",
                "-i",
                str(source),
                "-vf",
                (
                    "zscale=matrixin=gbr:matrix=bt709:transferin=linear:"
                    "transfer=iec61966-2-1:primariesin=bt709:primaries=bt709,"
                    "format=yuvj444p"
                ),
                "-frames:v",
                "1",
                "-update",
                "1",
                "-q:v",
                "2",
                str(args.output),
            ],
            check=True,
        )

    print(f"Wrote {args.output} ({sha256(args.output)})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

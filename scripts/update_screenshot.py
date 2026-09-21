from __future__ import annotations

import argparse
import pathlib
import struct
import subprocess
import sys
import tempfile
import zlib

try:
    from .draxul_paths import executable_path
except ImportError:  # Direct execution: scripts/ is the import root.
    from draxul_paths import executable_path


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parent.parent


def platform_output_path(root: pathlib.Path) -> pathlib.Path:
    if sys.platform.startswith("win"):
        return root / "screenshots" / "draxul-pc.png"
    if sys.platform == "darwin":
        return root / "screenshots" / "draxul-mac.png"
    return root / "screenshots" / "draxul-linux.png"


def default_scenario_path(root: pathlib.Path) -> pathlib.Path:
    return root / "tests" / "render" / "readme-hero.toml"


def build_command(root: pathlib.Path) -> list[str]:
    if sys.platform.startswith("win"):
        return ["cmake", "--build", str(root / "build"), "--config", "Debug", "--parallel"]
    return ["cmake", "--build", str(root / "build"), "--parallel"]


def draxul_path(root: pathlib.Path) -> pathlib.Path:
    return executable_path(root)


def run_command(command: list[str], cwd: pathlib.Path) -> None:
    completed = subprocess.run(command, cwd=cwd, check=False)
    if completed.returncode != 0:
        raise SystemExit(completed.returncode)


def parse_bmp_rgba(path: pathlib.Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if len(data) < 54 or data[:2] != b"BM":
        raise ValueError("not a BMP file")

    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    width = struct.unpack_from("<i", data, 18)[0]
    height = struct.unpack_from("<i", data, 22)[0]
    planes = struct.unpack_from("<H", data, 26)[0]
    bits_per_pixel = struct.unpack_from("<H", data, 28)[0]
    compression = struct.unpack_from("<I", data, 30)[0]

    if width <= 0 or height == 0 or planes != 1 or bits_per_pixel != 32 or compression != 0:
        raise ValueError("unsupported BMP format")

    top_down = height < 0
    abs_height = abs(height)
    expected_size = width * abs_height * 4
    pixel_bytes = data[pixel_offset : pixel_offset + expected_size]
    if len(pixel_bytes) != expected_size:
        raise ValueError("truncated BMP data")

    rows = []
    for row in range(abs_height):
        src_row = row if top_down else (abs_height - 1 - row)
        start = src_row * width * 4
        end = start + width * 4
        bgra = pixel_bytes[start:end]
        rgba = bytearray(len(bgra))
        for index in range(0, len(bgra), 4):
            rgba[index + 0] = bgra[index + 2]
            rgba[index + 1] = bgra[index + 1]
            rgba[index + 2] = bgra[index + 0]
            rgba[index + 3] = bgra[index + 3]
        rows.append(bytes(rgba))

    return width, abs_height, b"".join(rows)


def png_chunk(chunk_type: bytes, payload: bytes) -> bytes:
    crc = zlib.crc32(chunk_type)
    crc = zlib.crc32(payload, crc)
    return struct.pack(">I", len(payload)) + chunk_type + payload + struct.pack(">I", crc & 0xFFFFFFFF)


def write_png(path: pathlib.Path, width: int, height: int, rgba: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)

    scanlines = bytearray()
    stride = width * 4
    for row in range(height):
        scanlines.append(0)
        start = row * stride
        scanlines.extend(rgba[start : start + stride])

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    compressed = zlib.compress(bytes(scanlines), level=9)

    png = bytearray(PNG_SIGNATURE)
    png.extend(png_chunk(b"IHDR", ihdr))
    png.extend(png_chunk(b"IDAT", compressed))
    png.extend(png_chunk(b"IEND", b""))
    path.write_bytes(png)


def main() -> int:
    root = repo_root()

    parser = argparse.ArgumentParser(description="Update the platform screenshot using Draxul's render-test capture path.")
    parser.add_argument("--scenario", type=pathlib.Path, default=default_scenario_path(root))
    parser.add_argument("--output", type=pathlib.Path, default=platform_output_path(root))
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()

    scenario = args.scenario if args.scenario.is_absolute() else (root / args.scenario)
    output = args.output if args.output.is_absolute() else (root / args.output)

    if not scenario.exists():
        print(f"Scenario not found: {scenario}", file=sys.stderr)
        return 1

    if not args.skip_build:
        run_command(build_command(root), root)

    draxul = draxul_path(root)
    if not draxul.exists():
        print(f"Built app not found: {draxul}", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory(prefix="draxul-shot-") as temp_dir:
        temp_bmp = pathlib.Path(temp_dir) / "capture.bmp"
        command = [
            str(draxul),
            "--console",
            "--render-test",
            str(scenario),
            "--export-render-test",
            str(temp_bmp),
        ]
        run_command(command, root)

        width, height, rgba = parse_bmp_rgba(temp_bmp)
        write_png(output, width, height, rgba)

    print(f"Updated screenshot: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

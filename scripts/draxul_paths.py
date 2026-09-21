#!/usr/bin/env python3
"""Resolve Draxul build artifacts consistently for developer helpers."""

from __future__ import annotations

import argparse
import pathlib
import sys


def executable_path(
    root: pathlib.Path,
    *,
    platform: str = sys.platform,
    config: str = "Debug",
) -> pathlib.Path:
    build = root / "build"
    if platform.startswith("win"):
        configured = build / config / "draxul.exe"
        return configured if configured.exists() else build / "draxul.exe"
    if platform == "darwin":
        return build / "draxul.app" / "Contents" / "MacOS" / "draxul"
    return build / "draxul"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=pathlib.Path, required=True)
    parser.add_argument("--config", default="Debug")
    args = parser.parse_args()
    print(executable_path(args.root.resolve(), config=args.config))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

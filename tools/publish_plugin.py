#!/usr/bin/env python3
"""Atomically publish one fully staged native plugin package generation."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import time
import uuid


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--incoming", required=True, type=Path)
    args = parser.parse_args()

    root = args.root.resolve()
    incoming = args.incoming.resolve()
    if incoming.parent != root or incoming.name != ".incoming":
        raise SystemExit("incoming package must be <root>/.incoming")
    if not (incoming / "plugin.toml").is_file():
        raise SystemExit("incoming plugin package has no plugin.toml")

    build_id = f"{time.time_ns():020d}-{uuid.uuid4().hex[:8]}"
    inventory: dict[str, str] = {}
    for path in sorted(incoming.rglob("*")):
        if path.is_file():
            relative = path.relative_to(incoming).as_posix()
            inventory[relative] = hashlib.sha256(path.read_bytes()).hexdigest()
    (incoming / "package.json").write_text(
        json.dumps(
            {"schema_version": 1, "build_id": build_id, "files": inventory},
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )

    generations = root / "generations"
    generations.mkdir(parents=True, exist_ok=True)
    published = generations / build_id
    os.replace(incoming, published)

    pointer = root / "current.json"
    temporary = root / f"current.json.tmp-{uuid.uuid4().hex}"
    temporary.write_text(
        json.dumps({"schema_version": 1, "generation": build_id}, indent=2)
        + "\n",
        encoding="utf-8",
    )
    os.replace(temporary, pointer)
    retained = sorted(
        (path for path in generations.iterdir() if path.is_dir()),
        key=lambda path: path.name,
        reverse=True,
    )
    for stale in retained[3:]:
        shutil.rmtree(stale, ignore_errors=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

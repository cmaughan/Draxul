#!/usr/bin/env python3
"""Generate UML diagrams from C++ source via clang-uml and PlantUML.

Requirements: clang-uml, plantuml (optional — for rendering to PNG/SVG)
Usage:
    python scripts/gen_uml.py                         # generate + render all diagrams
    python scripts/gen_uml.py --diagram draxul_classes
    python scripts/gen_uml.py --puml-only             # skip render, just emit .puml
    python scripts/gen_uml.py --format svg            # render to SVG (default: png)
    python scripts/gen_uml.py --dry-run               # print commands without running
"""
from __future__ import annotations

import argparse
import platform
import shutil
import subprocess
import sys
from pathlib import Path


def find_tool(name: str) -> str:
    path = shutil.which(name)
    if path:
        return path

    if platform.system() == "Windows":
        # winget installs these to fixed locations not always on the current session PATH
        win_candidates: dict[str, list[Path]] = {
            "clang-uml": [
                Path(r"C:\Program Files\clang-uml\bin\clang-uml.exe"),
            ],
            "plantuml": [
                Path(r"C:\ProgramData\chocolatey\bin\plantuml.cmd"),
                Path(r"C:\Program Files\PlantUML\plantuml.exe"),
            ],
        }
        for candidate in win_candidates.get(name, []):
            if candidate.exists():
                print(f"  Found {name} at {candidate} (not on PATH — consider restarting your terminal)")
                return str(candidate)

    # plantuml on macOS may be a brew wrapper
    if name == "plantuml" and platform.system() == "Darwin":
        for p in [Path("/opt/homebrew/bin/plantuml"), Path("/usr/local/bin/plantuml")]:
            if p.exists():
                return str(p)

    install_hints = {
        "clang-uml": (
            "  macOS:   brew install clang-uml\n"
            "  Windows: winget install bkryza.clang-uml  (then restart your terminal)"
        ),
        "plantuml": (
            "  macOS:   brew install plantuml\n"
            "  Windows: choco install plantuml  OR  winget install PlantUML.PlantUML\n"
            "           (or use --puml-only to skip rendering)"
        ),
    }
    raise RuntimeError(f"'{name}' not found.\n" + install_hints.get(name, f"  Please install '{name}'."))


def ensure_tools_build_configured(repo_root: Path, dry_run: bool) -> None:
    """Ensure build-tools/compile_commands.json is up to date.

    Always re-runs cmake --preset clang-tools so that new dependencies or
    changed include paths are reflected without manual intervention.  CMake is
    idempotent — when nothing has changed the configure step completes quickly.
    """
    print("Refreshing build-tools/compile_commands.json (clang-tools preset)...")
    cmd = ["cmake", "--preset", "clang-tools"]
    print(f"> {' '.join(cmd)}")
    if not dry_run:
        subprocess.run(cmd, cwd=repo_root, check=True)


def run(cmd: list[str], *, cwd: Path | None = None, dry_run: bool) -> None:
    print(f"> {' '.join(str(c) for c in cmd)}")
    if not dry_run:
        subprocess.run(cmd, cwd=cwd, check=True)


def platform_compile_flags() -> list[str]:
    """Return clang-uml flags for platform-owned implicit include paths."""
    if platform.system() != "Darwin":
        return []

    sdk = subprocess.run(
        ["xcrun", "--show-sdk-path"], capture_output=True, text=True, check=True
    ).stdout.strip()
    compiler = next(
        (
            str(candidate)
            for candidate in (
                Path("/opt/homebrew/opt/llvm/bin/clang++"),
                Path("/usr/local/opt/llvm/bin/clang++"),
            )
            if candidate.exists()
        ),
        "clang++",
    )
    resource_dir = subprocess.run(
        [compiler, "--print-resource-dir"],
        capture_output=True,
        text=True,
        check=True,
    ).stdout.strip()
    return [
        "--add-compile-flag=-isysroot",
        f"--add-compile-flag={sdk}",
        "--add-compile-flag=-resource-dir",
        f"--add-compile-flag={resource_dir}",
    ]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", default="docs/uml", help="Output directory (default: docs/uml)")
    parser.add_argument("--diagram", metavar="NAME", help="Generate only this diagram (default: all)")
    parser.add_argument(
        "--format", default="svg", choices=["png", "svg", "eps", "pdf"],
        help="PlantUML render format (default: svg)",
    )
    parser.add_argument("--puml-only", action="store_true", help="Emit .puml files but do not render them")
    parser.add_argument("--dry-run", action="store_true", help="Print commands without executing them")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    output_dir = (repo_root / args.output).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    clang_uml = find_tool("clang-uml")
    if not args.puml_only:
        try:
            plantuml = find_tool("plantuml")
        except RuntimeError as exc:
            print(f"warning: {exc}")
            print("  Falling back to --puml-only mode. Install PlantUML to render diagrams.")
            args.puml_only = True
            plantuml = None
    else:
        plantuml = None

    ensure_tools_build_configured(repo_root, args.dry_run)

    # --- Run clang-uml (one diagram at a time to avoid a parallel-processing crash) ---
    config = str(repo_root / ".clang-uml")
    if args.diagram:
        diagrams = [args.diagram]
    else:
        # Enumerate diagram names so we can run them sequentially (-n one at a time).
        # Running all diagrams in one invocation triggers a thread-safety crash in
        # clang-uml 0.6.2 on Windows when two diagrams share translation units.
        out = subprocess.run(
            [clang_uml, "--config", config, "--list-diagrams"],
            capture_output=True, text=True,
        )
        diagrams = [
            line.strip().lstrip("- ").split()[0]
            for line in out.stdout.splitlines()
            if line.strip().startswith("-")
        ]
        if not diagrams:
            raise RuntimeError("Could not enumerate diagrams from .clang-uml config")

    compile_flags = platform_compile_flags()
    for name in diagrams:
        run(
            [clang_uml, "--config", config, *compile_flags, "-n", name],
            cwd=repo_root,
            dry_run=args.dry_run,
        )

    if args.puml_only or args.dry_run:
        if not args.dry_run:
            puml_files = list(output_dir.glob("*.puml"))
            print(f"\nPlantUML files written to {output_dir}:")
            for f in sorted(puml_files):
                print(f"  {f.name}")
        return 0

    # --- Render .puml → image ---
    assert plantuml is not None
    puml_files = (
        [output_dir / f"{args.diagram}.puml"]
        if args.diagram
        else sorted(output_dir.glob("*.puml"))
    )
    if not puml_files:
        print("No .puml files found — did clang-uml produce any output?")
        return 1

    fmt_flag = f"-t{args.format}"
    for puml in puml_files:
        cmd = [plantuml, fmt_flag, str(puml)]
        run(cmd, dry_run=args.dry_run)

    if not args.dry_run:
        rendered = sorted(output_dir.glob(f"*.{args.format}"))
        print(f"\nDiagrams written to {output_dir}:")
        for f in rendered:
            print(f"  {f.name}")

        system = platform.system()
        if rendered:
            first = rendered[0]
            if system == "Darwin":
                print(f"\nOpen with:  open {first}")
            elif system == "Windows":
                print(f"\nOpen with:  start {first}")
            else:
                print(f"\nOpen with:  xdg-open {first}")

    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(1)

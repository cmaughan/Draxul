#!/usr/bin/env python3
"""Generate a CMake target dependency graph and render it to SVG via Graphviz.

Requirements: cmake, dot (graphviz)
Usage:
    python scripts/gen_deps.py                       # output to docs/deps/
    python scripts/gen_deps.py --output docs/my      # custom output directory
    python scripts/gen_deps.py --exclude draxul-tests  # hide specific targets
    python scripts/gen_deps.py --prune SDL3-static    # keep node, drop its deps
    python scripts/gen_deps.py --dry-run              # print commands without running

External/imported targets (SDL3, freetype, harfbuzz, Vulkan, etc.) are excluded
automatically via CMakeGraphVizOptions.cmake in the repo root.
"""
from __future__ import annotations

import argparse
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path

# CMake auto-generated noise targets to strip from the graph
NOISE_PATTERNS = [
    r"ZERO_CHECK",
    r"ALL_BUILD",
    r"Continuous",
    r"Experimental",
    r"Nightly",
    r"NightlyMemoryCheck",
]


def find_tool(name: str) -> str:
    path = shutil.which(name)
    if path:
        return path

    # On Windows, graphviz winget install puts dot.exe in a fixed location
    # that isn't always added to the current session's PATH.
    if platform.system() == "Windows" and name == "dot":
        candidates = [
            Path(r"C:\Program Files\Graphviz\bin\dot.exe"),
            Path(r"C:\Program Files (x86)\Graphviz\bin\dot.exe"),
        ]
        for candidate in candidates:
            if candidate.exists():
                print(f"  Found {name} at {candidate} (not on PATH — consider restarting your terminal)")
                return str(candidate)

    install_hints = {
        "dot": (
            "  macOS:   brew install graphviz\n"
            "  Ubuntu:  apt install graphviz\n"
            "  Windows: winget install Graphviz.Graphviz  (then restart your terminal)"
        ),
        "cmake": "  Install CMake from https://cmake.org/download/",
    }
    raise RuntimeError(f"'{name}' not found.\n" + install_hints.get(name, f"  Please install '{name}'."))





def ensure_build_configured(repo_root: Path, dry_run: bool) -> None:
    cache = repo_root / "build" / "CMakeCache.txt"
    if cache.exists():
        return

    print("No CMake build directory found. Configuring...")
    preset = "mac-debug" if platform.system() == "Darwin" else "default"
    cmd = ["cmake", "--preset", preset]
    print(f"> {' '.join(cmd)}")
    if not dry_run:
        subprocess.run(cmd, cwd=repo_root, check=True)


def filter_dot(dot_text: str, exclude_patterns: list[str] | None = None) -> str:
    """Remove noisy CMake utility targets, the legend, and any user-excluded targets."""
    # Strip the legend subgraph
    result = _strip_subgraph(dot_text, "clusterLegend")

    # Collect all node IDs to remove: built-in noise + user exclusions
    all_patterns = list(NOISE_PATTERNS)
    if exclude_patterns:
        all_patterns.extend(re.escape(p) if not _is_regex(p) else p for p in exclude_patterns)

    if not all_patterns:
        return result

    noise_re = re.compile("|".join(all_patterns))

    # First pass: find node IDs whose labels match any pattern
    # CMake emits:  "node0" [ label = "target-name" ... ]
    node_def_re = re.compile(r'^\s*("node\w+")\s*\[.*?label\s*=\s*"([^"]+)"', re.IGNORECASE)
    excluded_ids: set[str] = set()
    for line in result.splitlines():
        m = node_def_re.match(line)
        if m and noise_re.search(m.group(2)):
            excluded_ids.add(m.group(1))

    if not excluded_ids:
        # No node IDs found — fall back to plain line filtering
        lines = [l for l in result.splitlines(keepends=True) if not noise_re.search(l)]
        return "".join(lines)

    # Second pass: drop node definitions and any edge that touches an excluded node
    edge_re = re.compile(r'^\s*("node\w+")\s*->\s*("node\w+")')
    lines = []
    for line in result.splitlines(keepends=True):
        # Drop node definition
        m = node_def_re.match(line)
        if m and m.group(1) in excluded_ids:
            continue
        # Drop edges to/from excluded nodes
        e = edge_re.match(line)
        if e and (e.group(1) in excluded_ids or e.group(2) in excluded_ids):
            continue
        lines.append(line)
    return "".join(lines)


def prune_deps_of(dot_text: str, prune_patterns: list[str]) -> str:
    """Keep named nodes but remove everything they depend on (their subtrees).

    In CMake graphviz output edges point from dependent -> dependency, so
    "dependencies of X" are nodes reachable by following outgoing edges from X.
    """
    if not prune_patterns:
        return dot_text

    prune_re = re.compile("|".join(
        re.escape(p) if not _is_regex(p) else p for p in prune_patterns
    ))

    node_def_re = re.compile(r'^\s*("node\w+")\s*\[.*?label\s*=\s*"([^"]+)"', re.IGNORECASE)
    edge_re = re.compile(r'^\s*("node\w+")\s*->\s*("node\w+")')

    lines = dot_text.splitlines(keepends=True)

    # Build label->id and id->label maps
    id_to_label: dict[str, str] = {}
    for line in lines:
        m = node_def_re.match(line)
        if m:
            id_to_label[m.group(1)] = m.group(2)

    # Build adjacency list (outgoing edges = dependencies)
    adj: dict[str, set[str]] = {nid: set() for nid in id_to_label}
    for line in lines:
        e = edge_re.match(line)
        if e:
            adj.setdefault(e.group(1), set()).add(e.group(2))

    # Find the root nodes to prune from
    roots = {nid for nid, label in id_to_label.items() if prune_re.search(label)}

    # BFS: collect all descendants (not including the roots themselves)
    to_remove: set[str] = set()
    queue = []
    for root in roots:
        queue.extend(adj.get(root, []))
    while queue:
        nid = queue.pop()
        if nid in to_remove or nid in roots:
            continue
        to_remove.add(nid)
        queue.extend(adj.get(nid, []))

    if not to_remove:
        return dot_text

    removed_labels = sorted(id_to_label[n] for n in to_remove if n in id_to_label)
    print(f"  Pruned {len(to_remove)} dependencies: {', '.join(removed_labels)}")

    # Remove node definitions and edges that touch removed nodes
    result = []
    for line in lines:
        m = node_def_re.match(line)
        if m and m.group(1) in to_remove:
            continue
        e = edge_re.match(line)
        if e and (e.group(1) in to_remove or e.group(2) in to_remove):
            continue
        result.append(line)
    return "".join(result)


# Node colouring by layer.
# Each entry: (label_regex, fillcolor, fontcolor)
# First match wins.
NODE_COLORS: list[tuple[str, str, str]] = [
    (r"^draxul$",                      "#2d6a9f", "white"),   # app executable — deep blue
    (r"^draxul-nvim-(protocol|transport)$", "#5b8dd9", "white"),  # integration — mid blue
    (r"^draxul-(window|renderer)$",    "#7ec8a4", "black"),   # platform/GPU layer — green
    (r"^draxul-(font|grid)$",          "#f0a868", "black"),   # domain layer — orange
    (r"^draxul-types$",                "#c9a8e0", "black"),   # shared types — purple
    (r"^draxul-tests$",                "#f4e04d", "black"),   # test targets — yellow
    (r"^draxul-rpc",                   "#f4e04d", "black"),   # test helpers — yellow
]


def colorize(dot_text: str) -> str:
    """Rewrite node attribute lines to add fillcolor based on layer."""
    # Matches:  "nodeN" [ label = "some text" shape = octagon ];
    # Label may contain \n sequences; line ends with ]; or ]
    node_def_re = re.compile(
        r'^(\s*"node\w+"\s*\[)([^\]]*label\s*=\s*"([^"]*)"[^\]]*)(\];?)(\s*)$',
        re.IGNORECASE,
    )
    compiled = [(re.compile(pat), fill, font) for pat, fill, font in NODE_COLORS]

    # Inject graph-level style defaults after the opening digraph line
    injected_defaults = False
    lines = []
    for line in dot_text.splitlines(keepends=True):
        # Inject defaults right after the `digraph ... {` opening line
        if not injected_defaults and re.match(r'^\s*digraph\b', line):
            lines.append(line)
            lines.append('    node [style=filled, fontname="Helvetica"]\n')
            lines.append('    edge [color="#888888"]\n')
            injected_defaults = True
            continue

        m = node_def_re.match(line)
        if m:
            label = m.group(3)  # raw label text (may contain \n)
            # Use only the first line of the label for matching
            first_line = label.split(r"\n")[0].strip()
            fill, font = "#e8e8e8", "black"  # default: light grey
            for pat, f, fc in compiled:
                if pat.search(first_line):
                    fill, font = f, fc
                    break
            attrs = m.group(2)
            # Strip any existing color/style attrs cmake may have set
            attrs = re.sub(r',?\s*\b(fillcolor|fontcolor|style)\s*=\s*"[^"]*"', "", attrs)
            attrs = attrs.rstrip(", ")
            new_line = f'{m.group(1)}{attrs}, fillcolor="{fill}", fontcolor="{font}" {m.group(4)}{m.group(5)}'
            lines.append(new_line if new_line.endswith("\n") else new_line + "\n")
        else:
            lines.append(line)
    return "".join(lines)


def _is_regex(pattern: str) -> bool:
    """Heuristic: treat pattern as regex if it contains regex metacharacters."""
    return bool(re.search(r'[.*+?^${}()|[\]\\]', pattern))


def _strip_subgraph(dot_text: str, subgraph_name: str) -> str:
    """Remove a named subgraph block (handles nested braces)."""
    marker = f"subgraph {subgraph_name}"
    start = dot_text.find(marker)
    if start == -1:
        return dot_text

    # Find the opening brace
    brace_pos = dot_text.index("{", start)
    depth = 0
    pos = brace_pos
    for pos, ch in enumerate(dot_text[brace_pos:], start=brace_pos):
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                break

    return dot_text[:start] + dot_text[pos + 1:]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", default="docs/deps", help="Output directory (default: docs/deps)")
    parser.add_argument("--format", default="svg", choices=["svg", "png", "pdf"], help="Output format (default: svg)")
    parser.add_argument(
        "--prune",
        action="append",
        default=[],
        metavar="PATTERN",
        help="Keep matched nodes but drop everything they depend on (collapse to leaf). "
             "Repeatable. Example: --prune SDL3-static --prune freetype",
    )
    parser.add_argument(
        "--exclude",
        action="append",
        default=[],
        metavar="PATTERN",
        help="Exclude targets whose names match PATTERN (substring or regex). Repeatable. "
             "Example: --exclude draxul-tests --exclude 'draxul-rpc.*'",
    )
    parser.add_argument("--dry-run", action="store_true", help="Print commands without executing them")
    args = parser.parse_args()

    cmake = find_tool("cmake")
    dot = find_tool("dot")

    repo_root = Path(__file__).resolve().parent.parent
    output_dir = (repo_root / args.output).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    raw_dot = output_dir / "deps.dot"
    filtered_dot = output_dir / "deps_filtered.dot"
    output_file = output_dir / f"deps.{args.format}"

    ensure_build_configured(repo_root, args.dry_run)

    # CMakeGraphVizOptions.cmake must be present in the build directory
    gv_options_src = repo_root / "CMakeGraphVizOptions.cmake"
    gv_options_dst = repo_root / "build" / "CMakeGraphVizOptions.cmake"
    if gv_options_src.exists() and not args.dry_run:
        import shutil as _shutil
        _shutil.copy2(gv_options_src, gv_options_dst)

    # Generate dot file
    cmd = [cmake, f"--graphviz={raw_dot}", "build"]
    print(f"> {' '.join(str(c) for c in cmd)}")
    if not args.dry_run:
        subprocess.run(cmd, cwd=repo_root, check=True, capture_output=True)

    # Filter noise
    if not args.dry_run:
        dot_text = raw_dot.read_text(encoding="utf-8")
        dot_text = prune_deps_of(dot_text, args.prune)
        dot_text = filter_dot(dot_text, args.exclude)
        dot_text = colorize(dot_text)
        filtered_dot.write_text(dot_text, encoding="utf-8")
        print(f"  Filtered noise targets -> {filtered_dot.name}")

    # Render
    cmd = [dot, f"-T{args.format}", str(filtered_dot if not args.dry_run else raw_dot), "-o", str(output_file)]
    print(f"> {' '.join(str(c) for c in cmd)}")
    if not args.dry_run:
        subprocess.run(cmd, check=True)

    if not args.dry_run:
        print(f"\nDependency graph written to:\n  dot : {raw_dot}\n  {args.format} : {output_file}")
        system = platform.system()
        if system == "Darwin":
            print(f"\nOpen with:  open {output_file}")
        elif system == "Windows":
            print(f"\nOpen with:  start {output_file}")
        else:
            print(f"\nOpen with:  xdg-open {output_file}")

    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(1)

# 36 Cell Codepoint Redundancy

## Why This Exists

`Cell` in `libs/draxul-grid/include/draxul/grid.h` stores both `std::string text` and `uint32_t codepoint`, where `codepoint` is always intended to be the first Unicode codepoint of `text`. `Grid::set_cell` populates both, but any direct mutation of `text` elsewhere would leave them silently out of sync. `codepoint` is used in only a handful of places, including `grid_tests.cpp` assertions and `text_service.cpp` which calls `utf8_first_codepoint` itself anyway.

Identified by: **Claude**.

## Goal

Remove `Cell.codepoint` and have the two or three remaining call-sites call `utf8_first_codepoint(cell.text)` on demand. This eliminates the latent consistency bug.

## Implementation Plan

- [x] Read `libs/draxul-grid/include/draxul/grid.h` and `libs/draxul-grid/src/grid.cpp` to locate all writes to `Cell.codepoint`.
- [x] Search across the codebase for all reads of `cell.codepoint` or `.codepoint` (app, tests, text_service).
- [x] Remove the `codepoint` field from `Cell`.
- [x] Replace each read site with a call to `utf8_first_codepoint(cell.text)` (the helper already exists in `unicode.h` or equivalent).
- [x] Remove any writes to `cell.codepoint` in `Grid::set_cell` and elsewhere.
- [x] Run `ctest` and `clang-format` on touched files.

## Sub-Agent Split

Single agent. Purely mechanical field removal with search-and-replace.

# Share binary catalog container framing

**Type:** refactor
**Priority:** 32
**Raised by:** Claude

## Goal

The `DXSTAR1`, `DXCLINE1`, and `DXCBND01` assets independently implement magic/version/count/record-size/bounds validation and Python writing. Share container framing while preserving semantic record formats and provenance.

## Implementation plan

- [x] Wait for active boundary-catalog work and record each current on-disk header/record byte layout with golden fixtures.
- [x] Define a small generic header reader/writer for magic, version, header size, record size/count, endian marker if needed, payload length, and optional checksum. (Implemented in `satview_catalog_container.{h,cpp}` / `scripts/satview_catalog_container.py`; little-endian only — no endian marker or checksum needed for v1, documented as such.)
- [x] Keep catalog-specific semantic validation in each loader. (Framing is delegated to `open_single_table_catalog` / `catalog_container_*`; finiteness/direction/rank checks stay in the constellation and boundary loaders.)
- [x] Add a matching Python helper used by all generator scripts.
- [x] Support existing versions without rewriting shipped assets in the first change. (Version 1 retained; shipped `.dxstar`/`.dxline`/`.dxbnd` load unchanged — no regeneration.)
- [x] If a new container version is justified, add deterministic migration/regeneration and attribution-preserving output. (Not triggered — version 1 retained, so no migration required this change.)
- [x] Document compatibility and maximum-size limits. (Header comment in `satview_catalog_container.h` and module docstring in `satview_catalog_container.py`.)

## Tests and acceptance

- [x] Golden-load every existing asset and compare record counts/selected values. (`tests/satview_catalog_container_tests.cpp` shipped-asset cases + `tests/satview_catalog_py_tests.py::ShippedAssetTests`.)
- [x] Cover bad magic/version/sizes/count overflow/truncation/non-finite semantic data. (C++ negative-framing + non-finite cases and direct helper unit tests; Python `ValidateHelperTests` / `PackHelpersTests`.)
- [x] Run generators twice and require byte-identical output. (`tests/satview_catalog_py_tests.py::GeneratorDeterminismTests`.)
- [ ] Existing runtime assets remain loadable on Windows and macOS. (macOS verified via full `ctest` on Apple M4 Pro; Windows pending CI — container code is platform-neutral little-endian with no OS-specific paths.)

## Dependencies and parallelism

Follows current catalog work. A data-format sub-agent can own C++/Python helpers while catalog owners review compatibility.

## Status

**2026-07-19** — Complete (modulo Windows CI verification).

- Shared C++ framing lives in `modules/satview/draxul-satview/src/satview_catalog_container.{h,cpp}` (wired into the SatView library CMake). The star and constellation loaders route framing through `open_single_table_catalog`; the multi-table boundary loader shares `catalog_container_range_fits` and the record/string limits. Catalog-specific semantic validation stays in each loader.
- Shared Python framing lives in `scripts/satview_catalog_container.py`, used by all three generators (`build_satview_{star,constellation,constellation_boundary}_catalog.py`).
- On-disk formats are unchanged (version 1); shipped assets load byte-for-byte as before — no regeneration, no version bump.
- Validation: `draxul` + `draxul-tests` build clean; full `ctest` 12/12 green (incl. new `draxul-satview-catalog-py-tests`); C++ `[satview][catalog]` 40 cases / 357 assertions; `do.py smoke` exit 0. All on macOS/Metal (Apple M4 Pro). Windows loadability is left for CI (platform-neutral little-endian format).

<model>GPT-5 Codex</model>

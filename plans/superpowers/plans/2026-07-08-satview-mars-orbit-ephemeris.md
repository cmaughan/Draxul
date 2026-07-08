# SatView Mars Orbit Ephemeris Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Import real Mars-relative spacecraft ephemerides so SatView can render known Mars orbiters around Mars instead of showing them only as catalog entries.

**Architecture:** Keep SATCAT as the inventory source for Mars-centered objects, but use mission SPICE SPK kernels as the geometry source. Generate a compact offline `mars_ephemeris.csv` with Mars-centered J2000 position/velocity samples, merge it into matching SATCAT records as `SampledEphemeris`, and let the existing sampled-ephemeris propagation/rendering path draw markers and tracks.

**Tech Stack:** C++20, CMake, Catch2, SatView sampled ephemeris CSVs, NAIF SPICE kernels via a Python generator, optional JPL Horizons probes for validation only.

---

## Sources

- Candidate inventory: CelesTrak SATCAT rows where `ORBIT_CENTER=MA`.
- Primary geometry: NAIF/PDS Mars mission SPICE archives, especially mission `spk` kernels and `extras/mk` metakernels.
- Useful source pages:
  - NAIF Mars mission data index: https://naif.jpl.nasa.gov/naif/data_mars.html
  - NAIF meta-kernel guidance: https://naif.jpl.nasa.gov/naif/furnsh_details.html
  - MRO SPICE archive overview: https://naif.jpl.nasa.gov/pub/naif/pds/data/mro-m-spice-6-v1.0/mrosp_1000/aareadme.htm
  - JPL Horizons API, for spot-check vector queries: https://ssd-api.jpl.nasa.gov/doc/horizons.html

## File Map

- Create: `assets/satview/catalog/mars_ephemeris_targets.json`
  - Lists Mars orbiter targets, identifiers, source archive URLs, SPICE IDs, metakernel paths, and generation windows.
- Create: `assets/satview/catalog/MARS_EPHEMERIS_ATTRIBUTION.md`
  - Records source archive links, kernel selection rules, generation date, and target coverage.
- Create: `assets/satview/catalog/mars_ephemeris.csv`
  - Generated sampled Mars-centered state vectors; do not hand-edit.
- Create: `scripts/build_satview_mars_ephemeris.py`
  - Downloads or locates SPICE kernels, samples state vectors relative to Mars, and writes the CSV plus attribution details.
- Modify: `modules/satview/draxul-satview/src/satview_catalog.cpp`
  - Add Mars sampled ephemeris loader/merge path parallel to lunar ephemeris.
- Modify: `modules/satview/draxul-satview/include/draxul/satview/satview_catalog.h`
  - Expose Mars ephemeris load/apply helpers if needed by tests or the catalog service.
- Modify: `modules/satview/draxul-satview/src/satview_catalog_service.cpp`
  - Apply bundled Mars ephemerides after SATCAT parsing and before final merge/status calculation.
- Modify: `modules/satview/draxul-satview/src/satview_propagation.cpp`
  - Verify sampled Mars records already flow through the sampled-ephemeris propagation path; only patch if body-relative anchoring assumes Moon.
- Modify: `modules/satview/draxul-satview/src/satview_host.cpp`
  - Verify Mars map/globe track projection uses Mars body transforms for sampled Mars records.
- Modify: `docs/satview/data-sources.md`
  - Add the Mars orbiter ephemeris source, cadence, and known coverage limitations.
- Modify: `docs/features.md`
  - Update SatView catalog/propagation feature text once Mars orbiters are renderable.
- Test: `tests/satview_catalog_tests.cpp`
- Test: `tests/satview_catalog_service_tests.cpp`
- Test: `tests/satview_propagation_tests.cpp`
- Test: `tests/satview_filter_tests.cpp`

## Task 1: Target Manifest and Source Policy

**Files:**
- Create: `assets/satview/catalog/mars_ephemeris_targets.json`
- Create: `assets/satview/catalog/MARS_EPHEMERIS_ATTRIBUTION.md`
- Modify: `docs/satview/data-sources.md`

- [ ] **Step 1: Add the manifest skeleton**

Create `assets/satview/catalog/mars_ephemeris_targets.json` with this shape:

```json
{
  "schema_version": 1,
  "central_body": "Mars",
  "frame": "J2000",
  "center": "Mars",
  "sample_step_seconds": 600,
  "targets": [
    {
      "id": "mro",
      "display_name": "Mars Reconnaissance Orbiter",
      "norad_catalog_id": 28788,
      "cospar_id": "2005-029A",
      "spice_id": -74,
      "source": "NAIF MRO SPICE archive",
      "archive_url": "https://naif.jpl.nasa.gov/pub/naif/pds/data/mro-m-spice-6-v1.0/mrosp_1000/",
      "metakernel_glob": "extras/mk/*.tm",
      "coverage_policy": "latest_reconstructed_or_predictive_window"
    }
  ]
}
```

- [ ] **Step 2: Add the first attribution note**

Create `assets/satview/catalog/MARS_EPHEMERIS_ATTRIBUTION.md` explaining that SPICE SPK kernels are the source of rendered Mars orbiter geometry, SATCAT is only the inventory source, and JPL Horizons is only a spot-check fallback.

- [ ] **Step 3: Document the policy**

In `docs/satview/data-sources.md`, add a Mars orbiter ephemeris section that states: SATCAT `ORBIT_CENTER=MA` rows remain catalog-only unless matched to a sampled SPICE-backed target.

## Task 2: SPICE Sampling Generator

**Files:**
- Create: `scripts/build_satview_mars_ephemeris.py`
- Create: `assets/satview/catalog/mars_ephemeris.csv`
- Modify: `assets/satview/catalog/MARS_EPHEMERIS_ATTRIBUTION.md`

- [ ] **Step 1: Write the generator CLI**

Implement:

```bash
python3 scripts/build_satview_mars_ephemeris.py \
  --targets assets/satview/catalog/mars_ephemeris_targets.json \
  --output assets/satview/catalog/mars_ephemeris.csv
```

The script should accept `--start`, `--stop`, `--step-seconds`, and `--cache-dir`, defaulting to a short current coverage window suitable for committed assets.

- [ ] **Step 2: Load SPICE kernels through metakernels**

Use `spiceypy` when installed. If it is missing, print a precise install hint and exit non-zero:

```text
spiceypy is required to build Mars ephemerides. Install with: python3 -m pip install spiceypy
```

- [ ] **Step 3: Sample Mars-centered state vectors**

For each target and sample time, call SPICE for target spacecraft relative to Mars in `J2000`, outputting kilometers and kilometers-per-second. The CSV columns should match the existing sampled ephemeris style:

```text
NORAD_CAT_ID,OBJECT_ID,OBJECT_NAME,CENTRAL_BODY,SOURCE,START_UNIX_SECONDS,UNIX_SECONDS,X_KM,Y_KM,Z_KM,VX_KM_S,VY_KM_S,VZ_KM_S
```

- [ ] **Step 4: Fail loudly on partial coverage**

If a target has no state samples inside the requested interval, omit that target from the CSV and record the omission in the attribution file with the SPICE error text.

## Task 3: Catalog Merge

**Files:**
- Modify: `modules/satview/draxul-satview/src/satview_catalog.cpp`
- Modify: `modules/satview/draxul-satview/include/draxul/satview/satview_catalog.h`
- Modify: `modules/satview/draxul-satview/src/satview_catalog_service.cpp`
- Test: `tests/satview_catalog_tests.cpp`
- Test: `tests/satview_catalog_service_tests.cpp`

- [ ] **Step 1: Write failing catalog tests**

Add tests proving that a Mars SATCAT `CatalogOnly` row becomes `SampledEphemeris`, `renderable=true`, and `central_body=CentralBody::Mars` when `mars_ephemeris.csv` contains matching samples.

- [ ] **Step 2: Add Mars ephemeris loader**

Mirror the lunar sampled ephemeris loader, but keep helper names explicit:

```cpp
[[nodiscard]] SampledEphemerisLoadResult load_bundled_mars_ephemeris();
void apply_mars_sampled_ephemeris(SatelliteCatalog& catalog, const SampledEphemerisCatalog& ephemeris);
```

- [ ] **Step 3: Apply Mars samples in the catalog service**

After SATCAT rows are retained and lunar dispositions are applied, merge Mars samples into matching Mars records. Do not synthesize Mars orbits from SATCAT period/apogee/perigee.

- [ ] **Step 4: Run focused catalog tests**

Run:

```bash
./build/tests/draxul-tests "[satview][catalog]"
./build/tests/draxul-tests "[satview][catalog-service]"
```

Expected: all matching tests pass and unmatched Mars rows stay `CatalogOnly`.

## Task 4: Propagation and Rendering

**Files:**
- Modify: `modules/satview/draxul-satview/src/satview_propagation.cpp`
- Modify: `modules/satview/draxul-satview/src/satview_host.cpp`
- Test: `tests/satview_propagation_tests.cpp`
- Test: `tests/satview_filter_tests.cpp`

- [ ] **Step 1: Write propagation tests**

Add a Mars sampled-ephemeris test that builds a propagation model with one Mars record and verifies the produced state keeps `central_body=CentralBody::Mars` and `solution_kind=OrbitSolutionKind::SampledEphemeris`.

- [ ] **Step 2: Verify Mars anchoring**

Inspect sampled ephemeris anchoring. If the existing anchor logic is Moon-specific, add a body switch so Mars samples remain Mars-centered while Moon samples keep their Moon anchoring behavior.

- [ ] **Step 3: Verify map projection**

Confirm Mars map/globe modes use the generic body transform already used for Mars surface markers. Patch only if Mars sampled orbit tracks project as Earth or Moon paths.

- [ ] **Step 4: Run focused propagation/filter tests**

Run:

```bash
./build/tests/draxul-tests "[satview][propagation]"
./build/tests/draxul-tests "[satview][filter]"
```

Expected: Mars sampled states renderable; Mars catalog-only rows remain hidden from scene paths unless matched.

## Task 5: Documentation and Verification

**Files:**
- Modify: `docs/features.md`
- Modify: `docs/satview/data-sources.md`

- [ ] **Step 1: Update user-facing docs**

Document that Mars orbit rendering uses SPICE-backed sampled ephemerides and that SATCAT alone is insufficient for drawing Mars-relative orbits.

- [ ] **Step 2: Run required verification**

Run:

```bash
cmake --build build --target draxul draxul-tests
./build/tests/draxul-tests "[satview]"
python3 do.py smoke
ctest --test-dir build -R draxul-render --output-on-failure
git diff --check
```

Expected: all commands exit 0. Note any third-party CMake or SDK warnings separately from Draxul failures.

- [ ] **Step 3: Commit**

Commit the completed Mars ephemeris work:

```bash
git add assets/satview/catalog/mars_ephemeris_targets.json \
  assets/satview/catalog/MARS_EPHEMERIS_ATTRIBUTION.md \
  assets/satview/catalog/mars_ephemeris.csv \
  scripts/build_satview_mars_ephemeris.py \
  modules/satview/draxul-satview \
  tests \
  docs/features.md \
  docs/satview/data-sources.md
git commit -m "Add SPICE-backed Mars orbiter ephemerides"
```

## Self-Review

- Spec coverage: the plan covers inventory, SPICE geometry, generated assets, catalog merge, propagation/rendering, docs, and verification.
- Placeholder scan: no TODO/TBD placeholders are present.
- Type consistency: uses existing SatView concepts: `CentralBody::Mars`, `OrbitSolutionKind::SampledEphemeris`, `CatalogOnly`, sampled ephemeris CSVs, and SatView tests.

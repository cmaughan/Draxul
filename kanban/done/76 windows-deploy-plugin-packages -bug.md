# Include native plugin packages in Windows deployment

**Summary:** Include bundled product views in Windows downloads so users can open the views provided by the build.

**Priority:** P1 — Windows downloads must contain their bundled product modules.
**Source:** `do.py`

**Evidence and trigger:** B19; deployment omits the published plugin directory required by runtime discovery.

- [x] **Investigate:** Compare staged package contents with deployment and archive contents.
  - Confirmed on 2026-10-08: `draxul_stage_registered_plugins()` publishes Windows
    packages to `<exe dir>/plugins/<id>/` (`current.json` plus
    `generations/<build id>/`, via `tools/publish_plugin.py`), and
    `PluginManager::bundled_plugin_directory()` discovers `<exe dir>/plugins`. The
    Windows branch of `_stage_deploy_payload()` copied only `assets`, `fonts`,
    `shaders`, and adjacent DLLs, so the archive had no plugin packages. macOS was
    unaffected: the whole `.app` (including `Contents/PlugIns`) is copied.
- [x] **Fix:** Copy complete published packages, including selection metadata, native modules, private dependencies, and assets.
  - `_stage_windows_plugin_packages()` copies each published package's `current.json`
    and its selected generation directory in full; `.incoming` scratch space, stale
    retained generations, and unpublished directories are excluded, and an unreadable,
    escaping, or incomplete selection fails the deploy instead of shipping a broken
    package.
- [x] **Acceptance:** A nested package survives archive extraction and is discoverable without independently installed user plugins.
  - `test_stage_windows_deploy_payload_ships_selected_plugin_packages` publishes two
    generations with the real publisher, stages and zips the payload, extracts the
    archive, resolves `current.json` as runtime discovery does, and verifies every
    `package.json` inventory hash including a nested asset and a private DLL.
- [x] **Validation:** Extend deployment coverage; run core aggregate tests and same-cache smoke, plus relevant packaged-product startup checks.
  - Deployment coverage extended (`tests/do_py_tests.py`, `DeployPackagingTests`);
    macOS Debug `do.py test debug` (56 CTest entries) passed apart from three load-sensitive flakes that pass in isolation or also fail on a pre-change binary, plus the known `draxul-do-py-tests` megacity AGENTS.md failure in submodule-less worktrees; `do.py smoke debug --skip-build` passed (2026-10-08). Windows deployment,
    isolated-profile archive discovery, and all seven packaged view startups
    are now verified below. No globally clean aggregate is claimed.

## Windows deployment evidence — 2026-10-08

- `py do.py deploy` passed with the corrected payload, producing
  `deploy/2026_10_08/draxul-2026_10_08-win.zip` (354,728,989 bytes).
  Release configure/build took 306.90 s for 145 build steps; the compilation
  portion was about 95.21 s, with the remainder in configure/environment setup.
- Extracted into `build-deploy-gate-extracted`, with a separate empty user
  profile at `build-deploy-gate-profile`. The packaged executable's
  `plugin list --json` reports all seven modules available, all
  `user_installed: false`, with module paths inside the extracted archive.
- Core aggregate evidence: initial full inventory 73/79 in 232.92 s;
  final core + Rezonality selection 64/70 in 306.89 s. Deployment tooling tests
  passed; unrelated runtime/font/scope/native-validation failures remain
  recorded rather than waived. Paired isolated-profile Debug smoke passed
  (35.273 s including environment setup).
- Final Release startup passed via `py do.py smoke release --skip-build`,
  exit 0 in 33.867 s including toolchain setup. The preceding attempt failed
  during resource-exhausted MSVC environment initialization (68.059 s), before
  launching the application; it was not an application smoke failure.
- Seven sequential packaged view captures exited 0 in 129.04 s total.
  The initial inspection helper incorrectly rejected top-down BMPs; offline
  header inspection confirms all seven are valid 960 by 760 images with height
  -760. No application rerun is needed for that helper error.
  Triangle, MegaCity, SatView, ScoreView and Flashcards were visually
  confirmed as real rendered views, not load-error placeholders. ScoreView
  uses an external test score and MegaCity an external source directory;
  their libraries and private dependencies come from the extracted packages.
- Rezonality's first capture was blank because this inspection helper supplied
  a nonexistent relative `examples/default` path. The product default is its
  bundled `examples/simple`. The corrected packaged startup passed in 7.80 s,
  exit 0, with its expected solid-red shader output visually verified against
  `examples/simple/screen.frag`. Its bundled compiler and shader assets were
  therefore exercised, not merely module discovery. No source rebuild was
  performed for this harness correction.
- Logs/captures: `build-ninja-debug/windows-gates/packaged-products.log`,
  `build-deploy-gate-extracted/{plugins,results}.json`, and the per-view BMP/logs
  in that extracted directory. These are local ignored validation artifacts.
- Corrected Rezonality evidence: `build-deploy-gate-extracted/results-rezonality.json`
  and `build-ninja-debug/windows-gates/packaged-rezonality-retry.log`.
  All deployment gates are complete; no production changes were needed in this
  audit. Test-owned smoke servers were shut down and no packaged client remains.

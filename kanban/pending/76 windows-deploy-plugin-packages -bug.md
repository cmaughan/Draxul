# Include native plugin packages in Windows deployment

**Summary:** Include bundled product views in Windows downloads so users can open the views provided by the build.

**Priority:** P1  
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
- [ ] **Validation:** Extend deployment coverage; run core aggregate tests and same-cache smoke, plus relevant packaged-product startup checks.
  - Deployment coverage extended (`tests/do_py_tests.py`, `DeployPackagingTests`);
    macOS Debug `do.py test debug` (56 CTest entries) passed apart from three load-sensitive flakes that pass in isolation or also fail on a pre-change binary, plus the known `draxul-do-py-tests` megacity AGENTS.md failure in submodule-less worktrees; `do.py smoke debug --skip-build` passed (2026-10-08). Outstanding: run
    `py do.py deploy` on Windows, extract the zip on a machine without user plugins,
    and confirm the bundled product views open.

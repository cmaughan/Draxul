# Keep embedded font and model dependencies out of install exports

**Summary:** Stop embedded font and model libraries from exporting their development packages so Windows builds can generate successfully with the new emoji font support.

**Priority:** P1
**Source:** `plugins/rezonality/CMakeLists.txt`, `cmake/FetchDependencies.cmake`.

- [x] Reproduce the Windows configure/generate failure with the shared zlib introduced for Fluent Emoji PNG decoding.
- [x] Disable unused Assimp, libpng, FreeType, and HarfBuzz development-package install exports and explicitly reuse the host-provided zlib target, retaining the macOS system-zlib path and Draxul SDK installation.
- [x] Supply ZLIB_INCLUDE_DIRS as well as ZLIB_INCLUDE_DIR for libpng's standalone header-generation preprocessor; target usage requirements alone do not reach this command.
- [x] Run the affected aggregate and same-cache smoke, then confirm the user's Release build/startup path. Build/startup gates pass; separate test failures are explicitly transferred to the linked cards below, not counted as passing.

Reproduction: `py do.py build debug` failed after configuration (80.1 seconds) and generation (1.4 seconds). Assimp's default `ASSIMP_INSTALL=ON` exports the already-found `zlib` and `zlibstatic` targets. zlib 1.3.1 exposes source/build include paths directly, which CMake rejects in an install export. No compiler invocation is required to trigger the failure. The plugin is mounted in Draxul and ships its native module rather than installing Assimp's development package.

The first repair disabled Assimp exports and exposed a second export-graph error: libpng and FreeType still exported references to zlibstatic, now absent from any export set. HarfBuzz similarly exported its reference to FreeType. All three font dependencies' SKIP_INSTALL_ALL switches are now limited to CMake variable blocks; only their source/build directory values propagate, keeping the parent project's SDK installation intact. This is tracked in the root board because it crosses the font dependency and product boundaries.

After generation succeeded, libpng's pnglibconf.out generation failed with C1083 (zlib.h missing): genout.cmake.in reads ZLIB_INCLUDE_DIRS, but the fetched dependency adapter supplied only ZLIB_INCLUDE_DIR. The adapter now provides both standard variables.

Validation on Windows, 2026-10-01:

- Final Debug configure/generate and compilation passed: two aggregate targets (`draxul-tests-core`, `draxul-tests-rezonality`), 197 remaining Ninja steps, about 119 seconds total. Earlier iterations intentionally failed while exposing the dependency chain: initial configure/generate 81.5 seconds, then 66.7, 69.3, and 68.1 seconds before libpng's header-generation failure. These were build diagnosis, not repeated successful aggregate runs.
- `py do.py test debug --rezonality`: 64/68 CTest entries passed in 196.42 seconds, including all 13 Rezonality entries and its seven render scenarios. Four failing entries were rerun serially in 34.35 seconds: server and remote-host entries passed, font and scope-selection failures repeated.
- Font follow-up: [63 joined-family-emoji-fallback -bug.md](../pending/63%20joined-family-emoji-fallback%20-bug.md). Test-scope follow-up: [64 windows-test-scope-selection -bug.md](../pending/64%20windows-test-scope-selection%20-bug.md). Timing follow-up: [65 windows-validation-timing -test.md](../pending/65%20windows-validation-timing%20-test.md).
- `py do.py smoke debug --skip-build`: first attempt timed out at 30 seconds while other validation was active; the same-cache retry passed within the wrapper's 30-second limit. The timing follow-up owns investigation, not a hidden passing claim.
- `draxul-render-unicode-view`: one additional non-overlapping scenario passed in 3.31 seconds. Fluent Kanban icon/color assertions also passed; the separate joined-family fallback assertion did not.
- `cmake --install build-ninja-debug --config Debug --component draxul-plugin-sdk --prefix build-ninja-debug/sdk-build-fix-check`: SDK headers, libraries and CMake package installed successfully in 0.6 seconds, proving the scoped dependency switches do not disable Draxul's SDK.
- `py do.py run release --console -- --smoke-test`: incremental Release build passed (six staging steps, 17.4 seconds), followed by successful startup/exit. The Release cache had already rebuilt successfully during this work; no redundant clean build was performed.
- macOS/remote CI not run on this Windows machine. The Apple system-zlib branch is retained; routine cross-platform CI remains responsible for that build.

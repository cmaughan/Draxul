# Keep the external SDK smoke on the current build and package format

Found by the complete integration inventory during `00 test-graph-and-scope -refactor.md`. Two stale fixture assumptions prevented the existing external SDK smoke from reaching its real native load/render checks.

- [x] Apply Draxul's internal CMake target policy only to the bundled triangle build; standalone SDK consumers do not have that internal helper.
- [x] Stage isolated SDK fixtures in `generations/sdk-smoke` with the current `current.json` pointer; preserve the whole package when copying it to the user plugin tier. No production compatibility fallback.
- [x] Include captured CLI output in discovery failures.
- [x] Verify the updated staging helper against a copied real app and module (available=true; under one second).
- [x] Pass the external SDK CTest: install public SDK, build a standalone plugin, discover bundled/user-tier copies, render a nonblank image.

The common helper is also used by ScoreView's optional cold extraction test; its payload paths now point into the generation directory. That expensive product-specific extraction is not required for this shared fixture correction. Normal Windows CI retains the external DLL build/discovery check; runtime Windows validation was not performed locally.

Validation: `ctest --test-dir build --build-config Debug --parallel 4 --tests-regex '^draxul-external-plugin-sdk-smoke$' --output-on-failure` passed 1/1 in 231.09 s. It built the independent consumer, loaded both bundled/user packages, and rendered a nonblank 960x640 image. The earlier aggregate passed the other 71 complete-inventory entries. Two earlier SDK attempts failed first on the missing internal CMake helper, then on the stale flat package layout; both are now resolved.

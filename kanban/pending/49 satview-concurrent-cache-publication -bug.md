# Preserve SatView caches during concurrent publication

**Severity:** HIGH  
**Reported by:** GPT-6 Astra

`plugins/satview/src/services/satview_catalog_service.cpp:105` gives concurrent writers the same temporary path. After one publishes it, another rename can fail and the fallback at `:124` removes the newly published cache.

**Investigation**

- [x] Interleave two catalog services writing the same payload and metadata paths.

**Fix strategy**

- [x] Use unique sibling temporary files.
- [x] Replace destinations atomically without deleting the last valid cache on failure.
- [x] Keep payload/metadata publication coherent across writers.

**Acceptance criteria**

- [x] Concurrent refreshes leave a complete usable cache.
- [x] Deterministic replacement failure through the production publication path preserves the previous destination and removes only the writer's private temporary file.
- [x] A real macOS filesystem replacement failure preserves the previous regular-file destination.
- [ ] Exercise the native Windows `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` failure path on Windows and confirm the previous destination remains readable.
- [x] Offline startup remains functional afterward.

## Validation

- macOS focused cache-publication filter: 24 assertions in 3 test cases passed.
- macOS `python3 do.py test debug --satview`: 26/26 core + SatView CTest entries passed.
- macOS `python3 do.py smoke debug --skip-build`: passed with the Metal renderer on Apple M5.
- The Windows implementation uses replace-in-place and shares the tested failure cleanup policy, but no Windows runtime was available for the remaining native filesystem check.

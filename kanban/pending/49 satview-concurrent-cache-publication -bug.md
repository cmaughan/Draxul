# Preserve SatView caches during concurrent publication

**Severity:** HIGH  
**Reported by:** GPT-6 Astra

`plugins/satview/src/services/satview_catalog_service.cpp:105` gives concurrent writers the same temporary path. After one publishes it, another rename can fail and the fallback at `:124` removes the newly published cache.

**Investigation**

- [ ] Interleave two catalog services writing the same payload and metadata paths.

**Fix strategy**

- [ ] Use unique sibling temporary files.
- [ ] Replace destinations atomically without deleting the last valid cache on failure.
- [ ] Keep payload/metadata publication coherent across writers.

**Acceptance criteria**

- [ ] Concurrent refreshes leave a complete usable cache.
- [ ] Failed replacement preserves the previous destination on Windows and macOS.
- [ ] Offline startup remains functional afterward.

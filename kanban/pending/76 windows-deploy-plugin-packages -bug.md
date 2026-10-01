# Include native plugin packages in Windows deployment

**Summary:** Include bundled product views in Windows downloads so users can open the views provided by the build.

**Priority:** 82  
**Severity:** HIGH  
**Source:** `do.py`

**Evidence and trigger:** B19; deployment omits the published plugin directory required by runtime discovery.

- [ ] **Investigate:** Compare staged package contents with deployment and archive contents.
- [ ] **Fix:** Copy complete published packages, including selection metadata, native modules, private dependencies, and assets.
- [ ] **Acceptance:** A nested package survives archive extraction and is discoverable without independently installed user plugins.
- [ ] **Validation:** Extend deployment coverage; run core aggregate tests and same-cache smoke, plus relevant packaged-product startup checks.

# Release native font resources during text-service replacement
**Severity:** MEDIUM  
**Source:** Codex #24; `libs/draxul-font/src/text_service.cpp:83` and `app/app.cpp:695`.

Reload move-assignment destroys old text-service internals without releasing their raw FreeType/HarfBuzz resources.

**Investigation**

- [ ] Audit resource ownership through initialization failure, shutdown, destruction, and move replacement.

**Fix strategy**

- [ ] Add automatic cleanup at the owning layer and preserve safe moved-from and repeated-shutdown behavior.

**Acceptance criteria**

- [ ] Repeated successful and failed reloads do not accumulate native resources or double-release them.
- [ ] Live hosts remain valid after replacement; run core aggregate tests and same-cache smoke.

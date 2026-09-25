# Release native font resources during text-service replacement
**Severity:** MEDIUM  
**Source:** Codex #24; `libs/draxul-font/src/text_service.cpp:83` and `app/app.cpp:695`.

Reload move-assignment destroys old text-service internals without releasing their raw FreeType/HarfBuzz resources.

**Investigation**

- [x] Audit resource ownership through initialization failure, shutdown, destruction, and move replacement.

**Fix strategy**

- [x] Add automatic cleanup at the owning layer and preserve safe moved-from and repeated-shutdown behavior.

**Acceptance criteria**

- [x] A repeated successful/failed reload loop and repeated shutdown run without double-release or stale-face failures.
- [x] Measure Windows Debug-CRT native allocations across reloads to rule out accumulation.
- [ ] Measure native allocations across reloads on macOS.
- [x] Run core aggregate tests and same-cache startup.
- [ ] Verify live hosts remain valid after font replacement.

**Implementation and evidence (2026-09-25)**

- `FontManager` now releases HarfBuzz font, FreeType face, and FreeType library
  in its destructor; explicit shutdown remains idempotent. Reinitialization
  closes the previous native handles before replacing them, and partial
  FreeType/HarfBuzz initialization failures clean up immediately.
- Default `TextService` move assignment can now destroy the old `Impl` safely:
  `FontResolver` destroys its shapers before each `FontManager` releases the
  underlying native face. `TextService::shutdown` is harmless on moved-from
  instances.
- Added a reload-loop regression that alternates successful and failed
  candidates, move-replaces the live service at a stable address, resolves a
  glyph after every attempt, and checks repeated/moved-from shutdown.
- The full `py do.py test debug --products` aggregate passed 49/49 CTest
  entries in 238.19 seconds, including the font reload-loop regression.
  Same-cache startup passed with a 90-second bound in about 45 seconds; the
  standard 30-second smoke wrapper timed out twice on the existing nine-pane
  Session.
- macOS native-resource measurement and live-host font-replacement inspection
  remain open before moving to done.

**Windows native allocation check (2026-09-25):** A Debug-CRT checkpoint
after warm-up measured zero retained normal-block bytes across 128 successive
successful `TextService` replacements, each resolving a glyph; the focused
`[font][reload]` run passed 2 cases and 64 assertions. macOS allocation
measurement and real-UI host inspection remain open.

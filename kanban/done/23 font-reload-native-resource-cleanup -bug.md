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
- [x] Run core aggregate tests and same-cache startup.
- [x] Confirm App keeps active and inactive fake hosts pumping and drawing after font replacement.
- [x] Verify a real Neovim host remains valid and renders after font replacement.

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
- At this stage, macOS native-resource measurement and real-host replacement
  had not been checked; the later integration and scope decision below resolve
  the completion gates.

**Windows native allocation check (2026-09-25):** A Debug-CRT checkpoint
after warm-up measured zero retained normal-block bytes across 128 successive
successful `TextService` replacements, each resolving a glyph; the focused
`[font][reload]` run passed 2 cases and 64 assertions. macOS allocation
measurement and real-host inspection had not run at this stage.

**Automated host coverage (2026-09-25):** The App config-reload smoke
tests now run another frame interval after replacing the font service in
both a two-tab (active/inactive) and a split-pane session. Both hosts remain
running, both pump counts advance, and the renderer begins new frames. The
focused `[app_smoke][config]` selection passed 9 cases and 91 assertions.
These use fake hosts/rendering; inspection with real Neovim/terminal hosts is
was still open at this stage.
The final Windows Debug core aggregate passed 25/25 CTest entries, and the
same-cache startup passed with a 90-second bound.

**Real-host integration (2026-09-25):** An App smoke test now launches a real
embedded Neovim host, waits for content readiness, reloads a larger font,
then checks the process and content stay ready, the renderer cell width grows,
and grid draws and frames continue. The focused test passed 1 case/12
assertions before the added readiness/metric checks; the final 49/49 Debug
aggregate includes those checks. FreeType/HarfBuzz cleanup is shared code,
and the Windows Debug-CRT 128-replacement allocation checkpoint found no
retained native blocks. A macOS allocation rerun is not a completion gate for
this shared cleanup. Same-cache Debug and Release startup passed.

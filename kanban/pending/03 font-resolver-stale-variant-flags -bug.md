# FontResolver: stale bold/italic variant flags on reinit

**Severity:** HIGH  
**File:** `libs/draxul-font/src/font_resolver.cpp:147–248`  
**Source:** review-bugs-consensus BUG-04 (gpt)

## Bug Description

`FontResolver::initialize()` sets `bold_loaded_ = true` (line 184), `italic_loaded_ = true` (line 214), and `bold_italic_loaded_ = true` (line 244) only on successful variant load, but **never resets them to `false` at entry**. If the user reloads config from Font A (which has bold) to Font B (which does not), `bold_loaded_` remains `true`. `FontSelector` then keeps serving glyphs from the old Font-A bold face, which is still in memory from the previous run.

`FontResolver::shutdown()` correctly resets all flags (lines 282–302), but `TextService::Impl::initialize()` never calls `shutdown()` before re-entering `resolver.initialize()`.

**Trigger:** Any config reload (manual reload or DPI change via `on_display_scale_changed`) that switches the active font family to one with fewer style variants than the previous family.

## Investigation

- [ ] Confirm `FontResolver::initialize()` does not reset variant flags at entry (lines 147–248)
- [ ] Confirm `TextService::Impl::initialize()` does not call `shutdown()` first (line 23–37 in `text_service.cpp`)
- [ ] Confirm `FontSelector` reads `bold_loaded_()` to decide whether to return the bold face (check `font_selector.h:143` and `FontResolver::bold_loaded()`)
- [ ] Reproduce: load a font with auto-detected bold, reload with a font that has no bold variant, verify bold text still uses the wrong face

## Fix Strategy

- [ ] At the top of `FontResolver::initialize()`, call `shutdown()` before probing new variants:
  ```cpp
  bool FontResolver::initialize(const TextServiceConfig& config, float point_size, float display_ppi)
  {
      shutdown();   // reset bold_loaded_, italic_loaded_, bold_italic_loaded_
      config_ = &config;
      ...
  }
  ```
- [ ] Alternatively (if calling `shutdown()` is too heavy), inline only the flag resets and face `shutdown()` calls at the top of `initialize()`
- [ ] Verify `GlyphAtlasManager::initialize()` also properly discards cached glyphs from the old face (called via `atlas_manager.initialize()` in `TextService::Impl::initialize()`)

## Acceptance Criteria

- [ ] After reloading from JetBrains Mono (has bold) to a custom font with no bold variant, bold text renders using the regular face (not the old bold face)
- [ ] After reloading from a font with no bold to one with bold, bold text renders correctly with the new face
- [ ] No FreeType/HarfBuzz objects leak across reinit cycles (ASAN clean under reload stress)
- [ ] `draxul-tests` font-related tests pass; run under `mac-asan` preset

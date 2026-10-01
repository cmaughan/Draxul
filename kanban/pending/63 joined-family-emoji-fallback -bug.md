# Restore a complete family-emoji fallback on Windows

**Summary:** Make joined family emoji display as one symbol when the bundled font lacks it, instead of falling back to separate characters.

**Priority:** P2
**Source:** `tests/font_resolver_style_tests.cpp:70`, discovered while validating [62 shared-zlib-install-exports -bug.md](../done/62%20shared-zlib-install-exports%20-bug.md).

- [x] Confirm the failure repeats independently of the aggregate's parallel execution.
- [ ] Inspect font selection and installed system-font support; determine whether the fallback logic or the test's platform-font assumption is wrong.
- [ ] Implement the appropriate correction without weakening the complete-grapheme requirement or removing Fluent Emoji support.
- [ ] Run the core aggregate, same-cache smoke, relevant Unicode snapshot, and final Release startup.

Windows 2026-10-01: `draxul-test-font-shard-0` failed both in the aggregate and serial retry. `an emoji font must compose the complete joined grapheme` expects one glyph for the family sequence but gets seven at line 87. The preceding Fluent Kanban icon/color checks pass, as does `draxul-render-unicode-view`. No font implementation was changed by the build fix; attribution to a specific earlier commit has not yet been established.

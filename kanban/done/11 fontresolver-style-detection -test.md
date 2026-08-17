# FontResolver style-font detection coverage

**Type:** test
**Disposition:** Covered

`tests/font_resolver_style_tests.cpp` covers automatic detection for every style,
warnings and fallback behavior, availability combinations, point-size changes,
selector rebinding, and cache behavior. The associated model refactor is recorded
in `kanban/done/30 font-style-model -refactor.md`.

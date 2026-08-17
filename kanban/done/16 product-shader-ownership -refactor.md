# Localize and gate product shaders

**Type:** refactor
**Completed:** 2026-08-15
**Disposition:** Superseded by the stronger external-plugin ownership boundary.

## Resolution

Commits `0d4a4135`, `9ff37992`, and `8456ce7b` moved product shaders, build policy,
runtime staging, assets, and tests into the MegaCity, SatView, and ScoreView plugin
repositories. Mounted products register their payload through
`cmake/DraxulPlugins.cmake`; root shader wiring now owns only core, Markdown, and
NanoVG inputs. Disabled or absent product checkouts therefore emit no product
shader payload.

# Diagnostics panel render coverage

**Type:** test
**Disposition:** Covered

`tests/render/panel-view.toml` exercises the diagnostics overlay as a required
regression scenario with Windows and macOS references. It is registered for CTest
and the standard compare/bless workflow in `tests/render/manifest.json`.

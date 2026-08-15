# Draxul — Features

The maintained feature inventory lives in **[docs/features.md](docs/features.md)** —
host types, configuration keys, keybindings, CLI flags, build options, and CI
infrastructure, kept current as features land.

The product plugins live in their own repositories (mounted as submodules
under `plugins/`), each carrying its own product documentation:

- [draxul-scoreview](https://github.com/cmaughan/draxul-scoreview) — ScoreView (`dev.draxul.scoreview`)
- [draxul-satview](https://github.com/cmaughan/draxul-satview) — SatView (`dev.draxul.satview`)
- [draxul-megacity](https://github.com/cmaughan/draxul-megacity) — MegaCity/BioView (`dev.draxul.megacity`)

This root file is intentionally just a pointer so the inventory has a single
source of truth. Do not add feature documentation here.

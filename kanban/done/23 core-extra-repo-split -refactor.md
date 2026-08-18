# Split Draxul core from product repositories

**Type:** refactor
**Disposition:** Superseded by per-product public repositories.

The proposed private sibling-overlay design was not adopted. SatView, MegaCity,
and ScoreView instead moved to their own repositories mounted under `plugins/` as
git submodules, with a generic SDK/plugin-support boundary. The delivered product
boundary is recorded in
`kanban/done/36 external-product-plugin-separation -refactor.md`; remaining
per-product CI work is tracked in
`kanban/pending/37 product-plugin-repo-split -refactor.md`.

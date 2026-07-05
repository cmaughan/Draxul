# Repository hygiene and feature-document source of truth

**Type:** refactor
**Priority:** 34
**Raised by:** Claude; supported by GPT/Codex

## Goal

Remove misleading/tracked root artifacts and establish `docs/features.md` as the single feature inventory without deleting provenance or user data blindly.

## Implementation plan

- [ ] Inspect history/references for `megacity-linux-drivers-mesh.bmp`, `key.txt`, `.!75583!.DS_Store`, root logs, and `NUL.obj`; classify required asset, historical evidence, local output, or accidental artifact.
- [ ] Move required large visual evidence under a documented asset/test location or external artifact store; otherwise untrack it deliberately.
- [ ] Rename or remove `key.txt` after confirming it is a debug log and contains no credential material.
- [ ] Add precise `.gitignore` rules for generated logs/objects without masking legitimate source assets.
- [ ] Make root `FEATURES.md` a short pointer to canonical `docs/features.md` or remove it after fixing inbound links.
- [ ] Add a lightweight hygiene check for forbidden root artifacts and duplicate feature-doc content.
- [ ] Generate a tracker index/status report that treats the `kanban` folder as authoritative and flags ambiguous unchecked boxes in `kanban/done` without rewriting historical cards automatically.
- [ ] Reopen the existing stale architecture-doc card separately; do not hide architecture corrections inside this cleanup.

## Tests and acceptance

- [ ] `git ls-files` contains no accidental OS/debug/build artifacts.
- [ ] Required assets remain discoverable with attribution and build/install wiring.
- [ ] Only `docs/features.md` contains the maintained feature inventory.
- [ ] Fresh configure/build does not depend on removed files.

## Dependencies and parallelism

Independent tooling/docs lane. A sub-agent may inventory history, but any deletion of large evidence should be reviewed explicitly.

<model>GPT-5 Codex</model>

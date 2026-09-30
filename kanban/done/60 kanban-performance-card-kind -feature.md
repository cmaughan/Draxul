# Show optimization cards as a distinct Kanban type

The optimization review generated 53 implementation cards with the `-refactor.md` suffix, which makes them indistinguishable from architecture refactors in the board.

- [x] Recognize `-perf.md` as a distinct card kind with a performance icon and color, while hiding the suffix from displayed names.
- [x] Rename this review's 53 optimization cards and repair their saved summary and manifest paths without changing card content or unrelated refactor cards.
- [x] Update the optimization consensus prompt so future implementation cards use `-perf.md`.
- [x] Run the scope-appropriate aggregate test and same-cache smoke; record the result here.
- [x] Read `**Priority/evidence:** Pn` on generated optimization cards so priority icons and sorting work without editing archived card content.
- [x] Remove gap cells between the type icon, priority icon, and card label.
- [x] Rerun the core aggregate and same-cache smoke after the priority and layout fix.

**Decision:** Use 📈 for performance and keep the existing priority icon beside it. The review's raw provider response remains an archived record of what it originally proposed; published summaries and manifests point to the renamed files.

**Validation:** `python3 do.py test debug` passed all 55 core CTest entries on macOS; `python3 do.py smoke --skip-build` passed from the same Debug cache. The four published review manifests list all 53 renamed cards with existing files and matching SHA-256 content hashes.

**Follow-up:** The review cards put P1/P2 under `Priority/evidence`, which the existing metadata reader skipped. Support that field directly; retain the standard `Priority` form for other cards.

**Follow-up validation:** `python3 do.py test debug` passed all 55 core CTest entries after the priority and layout change. A separate interactive Kanban launch switched the shared build tree to Release before the initial Debug smoke. After restoring Debug, `python3 do.py smoke debug` rebuilt the app and passed from the Debug cache. All 53 optimization cards declare a readable priority (7 P1 and 46 P2). An exploratory Release aggregate while the interactive app was active failed in server checkpoint coverage and timed out in several unrelated suites; it is not the gate for this Kanban change.

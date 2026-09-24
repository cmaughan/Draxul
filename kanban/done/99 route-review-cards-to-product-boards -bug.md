# Route product review cards to their owning Kanban boards

**Type:** bug
**Raised by:** user, 2026-09-24

## Problem

The review prompts and publisher only accept root `kanban/pending/` headings.
The 2026-09-24 bug review found product defects but wrote all 28 cards to the
root tracker, leaving product Kanban boards empty.

## Work

- [x] Accept and validate root and initialized product-submodule Kanban paths.
- [x] Route clearly product-owned review cards to their product tracker and normalize priorities per lane.
- [x] Update feature, bug, refactor, and skill prompts to request owning-board cards.
- [x] Move the current product cards, preserve the raw review response, and repair the published summary and manifests.
- [x] Add meaningful regression coverage for product routing and unsafe paths.
- [x] Run the aggregate review test gate and same-cache smoke.

## Notes

- Original review: `plans/reviews/runs/20260924T074533Z-review-bugs-7985d9ba`.
- Recovered consensus: `plans/reviews/runs/20260924T083605Z-review-bugs-summary-recovered-6daaa928`.
- Keep shared host/client/server defects in the root Kanban; product-owned defects belong under `plugins/<product>/kanban/pending/`.
- The recovered summary now points to 14 root cards and 14 product cards: MegaCity 1, PCBView 2, Rezonality 4, SatView 5, and ScoreView 2. Its manifest paths and hashes were checked against the files.
- The publisher validates initialized product submodules and routes root headings with a sole product `Source` path into the owning board; root and product priority collisions are resolved within their own lanes.
- Validation: `python3 -m unittest tests.review_skill_tests -q` (43 passed); `python3 do.py test debug` (25 CTest entries passed); `python3 do.py smoke debug --skip-build` (passed).

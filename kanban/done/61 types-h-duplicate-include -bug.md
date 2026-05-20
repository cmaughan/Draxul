---
# WI 61 — types.h Duplicate `#include <glm/glm.hpp>`

**Type:** bug  
**Priority:** high (trivial fix, signals review hygiene)  
**Raised by:** [C] Claude, [P] GPT  
**Created:** 2026-04-03  
**Model:** claude-sonnet-4-6

---

## Problem

`libs/draxul-types/include/draxul/types.h` contains a duplicate `#include <glm/glm.hpp>` on two separate lines. This is harmless to the compiler (include guards prevent double-processing) but signals the file has accumulated edits without careful review. It also slightly inflates compilation diagnostics and is confusing to new contributors.

---

## Investigation Steps

- [ ] Open `libs/draxul-types/include/draxul/types.h`
- [ ] Search for all occurrences of `#include <glm/glm.hpp>`
- [ ] Confirm exactly two exist; remove the duplicate
- [ ] Also check for any other duplicate includes in the same file

---

## Implementation

- [ ] Remove the redundant second `#include <glm/glm.hpp>` from `types.h`
- [ ] Scan the entire `draxul-types` include directory for any other duplicated includes while the file is open

---

## Acceptance Criteria

- [ ] `types.h` contains exactly one `#include <glm/glm.hpp>`
- [ ] No other duplicate includes remain in the file
- [ ] Project builds cleanly on both platforms

---

## Notes

No subagent needed — single-file change. Commit together with any other trivial hygiene fixes in `types.h` (e.g. `kAtlasSize` rename from WI 62 if done in the same pass).

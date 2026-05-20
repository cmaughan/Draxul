# 18 duplicate-planning-items-cleanup -refactor

**Priority:** LOW
**Type:** Refactor (planning hygiene)
**Raised by:** Gemini
**Model:** claude-sonnet-4-6

---

## Problem

The planning metadata has duplicate work-item IDs and numbering collisions that confuse unattended multi-agent workflows:

- `plans/work-items-icebox/20 url-detection-click -feature.md` and `22 url-detection-click -feature.md` — same topic, two files.
- `plans/work-items-icebox/19 guicursor-full-support -feature.md` and `23 guicursor-full-support -feature.md` — same topic, two files.
- `plans/work-items-complete/16 terminal-host-base-decomposition -refactor.md` and `17 terminal-host-base-decomposition -refactor.md` — same topic in complete.
- Number reuse across waves (multiple items numbered 00, 01, etc. in different batches) creates ambiguity when referencing items by ID alone.

This is an administrative task, safe to run in parallel with any code work.

---

## Implementation Plan

- [ ] List all files in `plans/work-items-icebox/` and `plans/work-items-complete/`.
- [ ] Identify all duplicates (same topic, different numbers).
- [ ] For each duplicate pair in icebox:
  - Read both files.
  - Merge content into the lower-numbered file (keep the richer description).
  - Delete the higher-numbered duplicate.
- [ ] For each duplicate pair in complete:
  - Verify both describe the same completed work.
  - Rename/remove one, keeping the other as the canonical record.
- [ ] Assess the number-reuse problem: consider adding a wave prefix (e.g., `W1-00`, `W2-00`) or just relying on full filenames rather than numbers in cross-references. Document the chosen convention in a `plans/README.md` or `CLAUDE.md` note.
- [ ] Update any references in `CLAUDE.md` or `plans/prompts/` that point to the removed file paths.
- [ ] No build or test changes needed.

---

## Acceptance

- No duplicate topic entries in icebox or complete directories.
- All remaining items have unique filenames.
- Any cross-references updated to point to the surviving canonical file.

---

## Interdependencies

- No code dependencies. Can run in parallel with all other items.
- Should be done before the next multi-agent work session to prevent confusion.

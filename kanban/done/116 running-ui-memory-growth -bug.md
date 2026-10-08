# Diagnose and fix growing UI memory

The user reports continuously growing memory with the live UI, potentially Rezonality, personal chat or another subsystem. Measure the real process, identify allocation ownership, fix the cause and verify bounded memory under the reproducing workload.

- [x] Capture live growth and isolate the responsible workload/allocation type.
- [x] Fix the responsible owner and add appropriate regression coverage.
- [x] Verify sustained memory behaviour, aggregate tests and same-cache startup.

Initial live target: UI PID 18987, server PID 35880; personal providers 35881–35883. UI RSS rose from about 337 MiB at 23 seconds to 594 MiB at 2m41s; server/provider RSS roughly stable. Measurements: /tmp/draxul-memory-trend.jsonl, /tmp/draxul-memory-initial.txt, /tmp/draxul-heap-initial.txt. Diagnosis in progress; do not assume chat or Rezonality is the cause yet.


## Root cause and fix

The personal chat NanoVG font loader used the absolute font path as its font name. Fontstash stores a maximum 63-byte name, truncating the configured bundled font path; subsequent full-path lookups therefore always failed and reloaded the 2,469,104-byte font file on every frame. The live heap showed 2,188 allocations of the rounded 2,416 KiB size, accounting for most of the 5.4 GiB footprint. Server and provider processes did not show that growth. This was introduced by the new personal chat renderer, not evidence of a Rezonality leak.

Use short stable per-context font names via personal_chat_font.h. The host replaces the NanoVG context when the primary font path changes, freeing old font data while permitting a new font. Regression coverage uses real NanoVG/Fontstash with no GPU, loads a real font through an 80-character directory name, checks the same font ID for 1,000 repeated frame lookups, validates text measurement and initializes a second independent context. Added the name-length pitfall to CLAUDE.md.

## Live verification

Restarted only UI PID 18987 as PID 27304; server PID 35880 and all three existing personal providers stayed running. The original UI had grown from roughly 337 MiB RSS at startup to over 5.4 GiB footprint. The rebuilt UI was exercised for three 60-second phases, including repeated forced chat focus/redraw requests:

- Chat: initial style/atlas warmup from 348.6 MiB to 380.6 MiB, then flat through the rest of the minute.
- Rezonality four-pane starship bridge: around 399.6 MiB; net RSS change -32 KiB over 60 seconds.
- Return to chat: around 391.3 MiB; net RSS change +144 KiB over 60 seconds, with no continuing upward slope.

Restored the existing terminal pane after measurement. Full trend: /tmp/draxul-fixed-memory-trend.jsonl; final heap: /tmp/draxul-heap-fixed.txt. The final heap contains only 2 of the 2,416 KiB blocks (down from 2,188), with a 506.6 MiB physical footprint. This verifies the reported rapid leak is gone under the reproducing workload, not that every application allocation has been exhaustively audited. No product submodule code was changed.

## Validation

Final `python3 do.py test release`: 56/56 core CTest entries passed in 21.85 s; final one-target aggregate build 6.83 s, same Release cache. Initial build configured in 16.0 s and generated in 1.6 s after the new private header appeared and failed because the new test lacked its TempDir include; corrected before the final aggregate. No separate focused pass. Same-cache isolated personal startup passed in 0.59 s. Personal render comparison passed in 1.72 s against the unchanged reference. Windows/remote CI not run; the real NanoVG long-path regression is portable and runs in regular CI. `git diff --check` clean. Changes remain uncommitted/unpushed.

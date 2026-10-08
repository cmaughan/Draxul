# Restore a complete family-emoji fallback on Windows

**Summary:** Make joined family emoji display as one symbol when the bundled font lacks it, instead of falling back to separate characters.

**Priority:** P2
**Source:** `tests/font_resolver_style_tests.cpp:70`, discovered while validating [62 shared-zlib-install-exports -bug.md](../done/62%20shared-zlib-install-exports%20-bug.md).

- [x] Confirm the failure repeats independently of the aggregate's parallel execution.
- [x] Inspect font selection and installed system-font support; determine whether the fallback logic or the test's platform-font assumption is wrong.
- [x] Implement the appropriate correction without weakening the complete-grapheme requirement or removing Fluent Emoji support.
- [x] Run the core aggregate, same-cache smoke, relevant Unicode snapshot, and final Release startup.

Windows 2026-10-01: `draxul-test-font-shard-0` failed both in the aggregate and serial retry. `an emoji font must compose the complete joined grapheme` expects one glyph for the family sequence but gets seven at line 87. The preceding Fluent Kanban icon/color checks pass, as does `draxul-render-unicode-view`. No font implementation was changed by the build fix; attribution to a specific earlier commit has not yet been established.

## Diagnosis and fix — Windows, 2026-10-08

**Both were wrong.** `C:\Windows\Fonts\seguiemj.ttf` was replaced by a Windows 11
update on 2026-09-20 (build 26200). HarfBuzz shaping of the installed face shows it
no longer composes any family ZWJ sequence: 👨‍👩‍👧‍👦 shapes to 4 glyphs (the members,
ZWJs absorbed and GPOS-arranged as a 2×2 group), the gender-neutral 🧑‍🧑‍🧒‍🧒 also to 4,
and only the single code point 👪 to 1. Bundled `FluentEmojiFlat.ttf` shapes the
sequence to 7 glyphs (it maps ZWJ to a full-width `space`). No installed font
composes the grapheme, so the test's "the platform supplies a complete family
glyph" assumption no longer holds on Windows. The fallback logic then rejected
every face and fell through to the primary text font, drawing four `.notdef`
boxes — visible in the 2026-10-08 Unicode capture.

Fix (`libs/draxul-font`):
- `FontSelector::select_partial_color_composition()` runs only after no color
  face composes a joined emoji cluster. Following UTS #51 (an unsupported ZWJ
  sequence displays as its components), it chooses the color face that renders
  every component in the **fewest** glyphs (Segoe: 4, ahead of Fluent's 7 with
  visible joiner spaces). A face that composes the whole grapheme shapes to one
  glyph and is still always preferred, so the complete-grapheme requirement is
  unchanged. Fluent stays first in the chain and still wins every cluster it composes.
- `GlyphCache::rasterize_cluster()` no longer pins color-emoji glyphs to one cell
  per code point (that spread the members across seven slots), and fits any
  color fallback cluster wider than its cells into them, as bitmap strikes
  already were.
- `tests/font_resolver_style_tests.cpp` now states the platform facts: macOS
  must compose the family in one glyph; on either platform the selector must
  choose the most complete color composition available, never Fluent's joiner
  spaces, and the rasterized cluster must stay within two cells.

Result: the family renders as one colored, cell-fitted group (Segoe's own 2×2
arrangement). `draxul-test-font` `[font]`: 56/56 test cases passed (3.45 s,
focused iteration). `draxul-render-unicode-view` passed (0.85% changed pixels)
before re-blessing under card 88.

## Validation summary — Windows, 2026-10-08

- Configure/build: fresh Ninja Debug cache after fast-forwarding 573 commits
  (1298 steps), then incremental rebuilds per fix; Release cache rebuilt
  (`py do.py build release`).
- Core + products aggregate (`py do.py test debug --products`): 85/90 CTest
  entries passed in 533 s. The five failures were unrelated to these changes:
  `personal_agent_tests.cpp:488` `remove_all` (deterministic, also failed in
  the earlier gate, tracked on `kanban/pending/67 personal-assistant-host -feature.md`);
  Rezonality native shard failing only on the stale `W:\p4` implicit-layer
  loader manifest (421/422); `draxul-render-flashcards-queue` aborted because
  the desktop pointer moved / capture was unavailable while the machine was in
  use; and two Windows encoding test bugs fixed in the same session and rerun
  green (`personal_assistant_host_tests.cpp` CJK `u8` literal, 5/5 cases;
  `draxul-rezonality-agent-layout` UTF-8 decoding, passed 5.9 s).
- Same-cache Debug smoke: `py do.py smoke debug --skip-build` passed (about
  6 s, isolated runtime; see `kanban/pending/65 windows-validation-timing -test.md`).
- Final Release startup: `py do.py smoke release --skip-build` passed in 4.8 s
  from the Release cache (isolated runtime, owned server shut down).
- Remote CI not run; macOS/Metal paths rely on normal cross-platform CI.

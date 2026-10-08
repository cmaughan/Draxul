# Open links from personal assistant replies

Preserve Markdown link destinations through wrapped layout and make their labels visibly clickable in personal assistant bubbles. Open HTTP/HTTPS links with the existing platform browser launcher. Preserve source copying and avoid launching other URI handlers from assistant text.

- [x] Preserve linked spans through emphasis, wrapping, lists and tables.
- [x] Style and hit-test visible links in personal chat, including scroll/clip coordinates.
- [x] Validate link boundaries and render appearance; aggregate and same-cache smoke.
- [x] Update documentation and record results.


## Implementation

Markdown text runs now carry their link destination and measured width through styled wrapping, long-word splitting and table layout. Adjacent spans only merge when both style and destination match. A shared hit test identifies linked runs by their laid-out bounds.

Personal chat adds accent colour and underline decorations to web links, returns a pointer cursor on hover, and uses the existing IWindow::open_url platform browser launcher on left-button press. Hit testing uses the same physical-pixel history viewport and scrolled rows as rendering; clipped text/header/composer cannot trigger links. Right-click bypasses navigation and retains whole-message source copy. Failed OS launches produce a toast. Only HTTP/HTTPS URLs without raw control characters/whitespace are actionable; other URI handlers and relative file paths are not launched. Ordinary terminal agents and document-viewer input behaviour are unchanged.

Parser-to-layout integration covers nested bold labels, multiple destinations, wrapped/long labels, lists, tables, literal inline code, surrounding unlinked text, shifted rows and URL scheme/control validation. The render fixture includes a web link; visually inspected its colour and underline in `/tmp/draxul-chat-links.png`. Host navigation uses the existing tested window/browser abstraction; an actual browser tab was not launched during automated validation.

## Validation

2026-10-07: one `draxul-tests-core` build passed in 18.30 s using the existing Release cache, no configure/generate rerun. `python3 do.py test release`: 56/56 core CTest entries passed in 22.68 s, including the new Markdown link cases; no focused overlap or test retries. Same-cache isolated personal startup passed in 0.54 s. Updated personal-assistant render reference inspected and final comparison passed in about 1.4 s. No standalone Markdown snapshot is registered on this cache; shared layout/draw-list suites ran in the aggregate. Windows/remote CI not run. `git diff --check` clean.

Restarting the GUI loads this interaction change; server/provider restarts are not needed. Card/docs updated. Changes remain uncommitted/unpushed.

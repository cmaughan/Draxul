# Fluent Emoji Flat

Unmodified `dist/FluentEmojiFlat.ttf` from [fluent-emoji-webfont](https://github.com/tetunori/fluent-emoji-webfont), commit `b7fc5ad4ceb3c7d665087040073d4b64490af96f`.

SHA-256: `951da552e63bd9e6b4ee375fe711a0cf523553568dd9c203888a43f872ab0dbb`

Font packaging: Tetsunori Nakayama, MIT; see `FluentEmoji-LICENSE.txt`.
Artwork: Microsoft, MIT; see `FluentEmoji-Artwork-LICENSE.txt` (from Microsoft/fluentui-emoji commit `62ecdc0d7ca5c6df32148c169556bc8d3782fca4`).

Draxul uses the embedded PNG color bitmaps through FreeType and resizes them to the active text size. The font also contains COLRv1 and SVG tables; Draxul does not need those renderers. The bundled monospace font uses this sibling font before system emoji fonts by default. Explicit `fallback_paths` replace that default chain.

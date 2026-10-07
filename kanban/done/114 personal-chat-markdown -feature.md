# Render Markdown in personal assistant replies

Assistant answers currently display literal Markdown markers in plain NanoVG text. Reuse the existing Markdown parser, layout, rich text and native GPU renderer inside assistant bubbles. Keep the composer and user messages literal and copy the original source text.

- [x] Embed shared Markdown rendering with wrapped bubble sizing and history clipping.
- [x] Cache message layout and preserve scrolling, streaming updates and source copy.
- [x] Cover formatted replies in the render fixture; run aggregate and same-cache smoke/render checks.
- [x] Update feature documentation and record implementation/validation.


## Implementation

PersonalAssistantHost embeds the existing MD4C parser, Markdown layout, RichTextService, draw-list builder and native MarkdownRenderPass. NanoVG still draws bubble backgrounds, plain user/system messages and controls. Assistant Markdown is drawn in a separate pass clipped to the assistant column and history viewport, protecting the header/composer and neighbouring user messages. Both Metal and Vulkan use their existing Markdown implementations; no separate platform renderer was added.

Bubble heights follow the laid-out rows, with chat-specific compact line spacing and outer padding. Parsed/layout results are cached by message ID, source text, width, scale and point size; removed history entries are pruned. Streaming updates and resizing invalidate affected layouts. Scrolling reuses them. Right-click copy continues to use original source text. Links/images retain the current document renderer's capabilities; this does not add a browser or clickable link navigation.

The personal-assistant render fixture now exercises multiple bubbles, history clipping, a heading, bold and italic spans, a bulleted list and fenced code. Height was increased to show both the user bubble and the full formatted answer. Inspected `/tmp/draxul-chat-markdown-final.png`. Docs/features.md, docs/personal-assistant.md and docs/module-map.md describe the shared rendering path.

## Validation

2026-10-07: initial app build for visual iteration passed in 29.75 s total, including automatic same-cache configure 16.5 s and generate 2.0 s after the link dependency changed (about 11.25 s remaining build/wrapper time). After adjusting chat line spacing, the final one-target `draxul-tests-core` build passed in 9.00 s with no further configure. `python3 do.py test release`: all 56 core CTest entries passed in 22.29 s. No focused test overlap or failed aggregate.

Same-cache isolated personal startup passed in 0.86 s and its server shut down cleanly. The updated render reference was visually inspected, then `draxul-render-personal-assistant` passed in 1.4 s. Two deliberate render-reference generations were used for the visual iteration; window activation was disabled through SDL hints to prevent live typing reaching the fixture. Existing parser/layout/rich-text coverage runs in the aggregate. Windows/remote CI not run here. `git diff --check` clean.

Live assistants were left running; restarting only the GUI loads this rendering change without restarting their provider processes. Changes remain uncommitted/unpushed.

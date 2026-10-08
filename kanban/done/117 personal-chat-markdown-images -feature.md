# Show Markdown chart images in personal chat

Assistant replies contain Markdown image links to generated local PNG charts, but the existing layout renders only their alt text. Render these images in chat, fitting the bubble width without distortion. Preserve the original reply for copying and keep ordinary agent terminals unchanged.

- [x] Add image-aware shared Markdown layout and personal chat loading/rendering.
- [x] Cover paths, decoding failures, resizing and bounded image lifetime with regression tests.
- [x] Verify a rendered chart, core aggregate, and same-cache startup; document behavior.

Support local absolute paths, local file URLs and paths relative to the personal agent's directory. Load/decode off the UI thread, upload only visible images, and bound image memory. Unsupported or missing images must show a useful placeholder.


## Implementation and behavior

MD4C image spans now retain their `src`; shared Markdown layout exposes image rows through an opt-in size callback and preserves surrounding text, list indentation and linked-image destinations. Personal chat uses the existing NanoVG image path on both Metal and Vulkan, keeping the native Markdown text pass. Images fit available width without changing aspect ratio; scrolling clips them to history. Other Markdown consumers continue to show alt text until they supply an image renderer.

The private personal-chat image loader uses the already-fetched stb decoder with private symbols (no new dependency or duplicate global decoder exports). Supports local PNG/JPEG/BMP/GIF first frame, absolute paths, local file URLs, agent-relative paths, percent escapes and UTF-8 paths through the shared filesystem helper. Network/data URLs and SVG show an unsupported-image placeholder; they are not fetched. Missing or damaged files also show a labeled reason. One background worker loads visible images; 16 MiB input, 8 megapixel image and 64 MiB visible GPU limits bound resources. CPU decoded pixels are discarded after upload, hidden textures are deleted, and dimensions stay cached to preserve layout. Context replacement/shutdown clears resource identities.

## Validation and evidence

- Parser-to-layout regression covers image paths/alt text, proportional fitting, text ordering, list images, linked-image hit testing, visible-row selection and text-only hosts.
- Real PNG loading covers escaped/Unicode paths, file URLs, missing files, directories, corrupt files and file-size limits. Real NanoVG callbacks verify one upload across 100 draws, release on scrolling out of view, and reloading on return.
- Added deterministic `tests/render/fixtures/personal-chat-chart.png`; inspected and blessed the personal-chat screenshot and passed its comparison. Snapshot includes formatted prose, a web link, a code block and the chart.
- Live verification: restarted UI PID 27304 as 46484, preserving server PID 35880 and providers 35881–35883. Focused existing Stocks pane-202 and visually verified its real SOL Heikin Ashi PNG alongside the price table and clickable source links. Screenshot: `/tmp/draxul-live-chart.png`; fixture screenshot: `/tmp/draxul-chat-images.png`.
- Final `python3 do.py test release`: 56/56 core CTest entries passed in 23.82 s; incremental build 12.43 s for one aggregate target in the existing Release Makefiles cache. Initial configure/generate took 16.1/1.5 s. Same-cache isolated personal startup passed in 0.48 s; render comparison passed in 1.40 s. No Windows/remote CI run; inspected both NanoVG backends' image resource paths and added portable tests to ordinary CI.
- Initial aggregate exposed missing parser `src` extraction and an incomplete fake-render callback in the new image lifetime test; both corrected before the final passing aggregate. No additional build tree or focused test pass; debugger used only to locate the fake callback failure. Documentation updated; no product submodule changes. Changes remain uncommitted/unpushed.

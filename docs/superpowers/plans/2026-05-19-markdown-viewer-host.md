# Markdown Viewer Host Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a Draxul-native markdown viewer host that loads a markdown file with `--host markdown --source <file>`, parses it, lays it out with variable-height rows, and renders it through Draxul's own FreeType/HarfBuzz/font-atlas/GPU pipeline.

**Architecture:** Add a non-grid `MarkdownHost` that owns a parsed markdown document, a variable-row flow layout, a scroll model, and a custom rich-text render pass. The terminal grid remains unchanged; markdown uses a new row/document layout where headings, code blocks, front matter, callouts, and paragraphs each choose their own font size, spacing, and background geometry.

**Tech Stack:** C++20, MD4C for initial markdown parsing, Draxul FreeType/HarfBuzz font stack, a new multi-size rich text atlas, Draxul `IRenderPass` for Vulkan/Metal drawing, Catch2 tests, existing host registry and CLI plumbing.

---

## Design Decisions

- Do not use ImGui, WebView, HTML, or the terminal grid renderer.
- Keep markdown viewer logic outside `app/`; app changes should only register the host and route CLI/source options.
- Treat "grid row" as a flow-layout row, not a terminal cell row:
  - A row has `y`, `height`, `baseline`, and typed draw content.
  - A heading row can be 42 px tall while a paragraph row is 20 px tall.
  - Scrolling is pixel-based, with an index over variable-height rows for fast culling.
- Use a parser abstraction from day one so MD4C can be swapped or augmented later for editor-grade source mapping.
- Use one rich-text atlas for all markdown text sizes, separate from the terminal atlas. The terminal `TextService` should remain stable.
- MVP is viewer-only: no text cursor, no selection editing, no markdown rewriting.

## Target User Experience

Command:

```powershell
.\build\Release\draxul.exe --host markdown --source README.md
```

Viewer behavior:

- Loads the source markdown file once on host startup.
- Shows a readable document surface with margins and constrained content width.
- Shows YAML front matter as a metadata block at the top.
- Renders headings with larger text, heavier weight, spacing, and optional divider styling.
- Renders paragraphs with wrapping and inline emphasis, strong, links, inline code, and code spans.
- Renders lists, task lists, blockquotes, fenced code blocks, thematic breaks, callout-style blockquotes, and basic tables.
- Shows a scrollbar when content height exceeds viewport height.
- Handles mouse wheel, page up/down, home/end, and viewport resize.
- Shows a useful error page for missing files, read failures, or parser failures.

## File Structure

### New Library: `libs/draxul-markdown`

Responsible for markdown parsing, document AST, Obsidian-like markdown extensions, layout, and markdown host state.

- Create: `libs/draxul-markdown/CMakeLists.txt`
- Create: `libs/draxul-markdown/include/draxul/markdown/markdown_document.h`
- Create: `libs/draxul-markdown/include/draxul/markdown/markdown_parser.h`
- Create: `libs/draxul-markdown/include/draxul/markdown/markdown_theme.h`
- Create: `libs/draxul-markdown/include/draxul/markdown/markdown_layout.h`
- Create: `libs/draxul-markdown/include/draxul/markdown/markdown_scroll.h`
- Create: `libs/draxul-markdown/include/draxul/markdown/markdown_host.h`
- Create: `libs/draxul-markdown/src/markdown_document.cpp`
- Create: `libs/draxul-markdown/src/markdown_parser_md4c.cpp`
- Create: `libs/draxul-markdown/src/markdown_front_matter.cpp`
- Create: `libs/draxul-markdown/src/markdown_theme.cpp`
- Create: `libs/draxul-markdown/src/markdown_layout.cpp`
- Create: `libs/draxul-markdown/src/markdown_scroll.cpp`
- Create: `libs/draxul-markdown/src/markdown_host.cpp`
- Create: `libs/draxul-markdown/src/markdown_render_pass.h`
- Create: `libs/draxul-markdown/src/markdown_render_pass_vk.cpp`
- Create: `libs/draxul-markdown/src/markdown_render_pass_metal.mm`
- Create: `libs/draxul-markdown/src/markdown_render_pass_null.cpp`

### Font Layer Changes

Responsible for variable-size text shaping and rasterization into a single rich-text atlas.

- Create: `libs/draxul-font/include/draxul/rich_text_service.h`
- Create: `libs/draxul-font/src/rich_text_service.cpp`
- Modify: `libs/draxul-font/CMakeLists.txt`

Keep existing `TextService` behavior intact for terminal/grid hosts.

### Renderer Interface Usage

No broad renderer redesign is required. The markdown render pass records through existing `IFrameContext::record_render_pass()` and static-casts `IRenderContext` to `VkRenderContext` or `MetalRenderContext`, matching the MegaCity/NanoVG pattern.

- Modify: `libs/draxul-renderer/CMakeLists.txt` only if shared shader packaging needs a new shader install path.
- Create: `shaders/markdown_text.glsl`
- Create: `shaders/markdown_text.metal`

### Host Wiring

- Modify: `libs/draxul-types/include/draxul/host_kind.h`
- Modify: `app/main.cpp`
- Modify: `app/command_palette.cpp`
- Modify: root `CMakeLists.txt`
- Modify: `docs/features.md`

Do not register `MarkdownHost` from `libs/draxul-host/src/host_factory.cpp`. The base host library should not depend on the concrete markdown host library; register markdown from `app/main.cpp`, matching the optional/non-core host pattern.

### Tests

- Create: `tests/markdown_parser_tests.cpp`
- Create: `tests/markdown_front_matter_tests.cpp`
- Create: `tests/markdown_layout_tests.cpp`
- Create: `tests/markdown_scroll_tests.cpp`
- Create: `tests/rich_text_service_tests.cpp`
- Create: `tests/markdown_host_tests.cpp`

The test target auto-discovers `*_tests.cpp`, so new test files do not need manual listing after CMake reconfigure.

---

## Core Data Model

Use explicit document types instead of pushing parser callbacks directly into layout.

```cpp
namespace draxul::markdown
{

struct SourceSpan
{
    size_t byte_offset = 0;
    size_t byte_length = 0;
    int start_line = 0;
    int end_line = 0;
};

enum class InlineKind
{
    Text,
    Emphasis,
    Strong,
    Code,
    Link,
    Image,
    SoftBreak,
    LineBreak,
};

struct Inline
{
    InlineKind kind = InlineKind::Text;
    std::string text;
    std::string destination;
    std::vector<Inline> children;
    SourceSpan source;
};

enum class BlockKind
{
    Document,
    FrontMatter,
    Paragraph,
    Heading,
    ThematicBreak,
    BlockQuote,
    Callout,
    List,
    ListItem,
    TaskItem,
    CodeBlock,
    Table,
    TableRow,
    TableCell,
    HtmlBlock,
};

struct FrontMatterEntry
{
    std::string key;
    std::string value;
};

struct Block
{
    BlockKind kind = BlockKind::Paragraph;
    int heading_level = 0;
    bool ordered = false;
    bool checked = false;
    std::string language;
    std::string literal;
    std::string callout_type;
    std::string callout_title;
    std::vector<FrontMatterEntry> front_matter;
    std::vector<Inline> inlines;
    std::vector<Block> children;
    SourceSpan source;
};

struct Document
{
    std::filesystem::path source_path;
    std::string source_text;
    std::vector<Block> blocks;
    std::vector<std::string> warnings;
};

} // namespace draxul::markdown
```

## Variable-Height Row Model

Rows are the future editor primitive. They let us do big headings now and hit testing later.

```cpp
namespace draxul::markdown
{

struct StyleId
{
    uint16_t value = 0;
};

struct TextRun
{
    std::string text;
    StyleId style;
    float x = 0.0f;
    float baseline = 0.0f;
    SourceSpan source;
};

struct Decoration
{
    enum class Kind { Background, BorderLeft, Divider, Checkbox, Bullet, ScrollbarThumb };
    Kind kind = Kind::Background;
    Rect rect;
    Color color;
    float radius = 0.0f;
};

struct LayoutRow
{
    float y = 0.0f;
    float height = 0.0f;
    float baseline = 0.0f;
    BlockKind source_kind = BlockKind::Paragraph;
    std::vector<TextRun> runs;
    std::vector<Decoration> decorations;
    SourceSpan source;
};

struct LayoutDocument
{
    float content_width = 0.0f;
    float content_height = 0.0f;
    std::vector<LayoutRow> rows;
};

} // namespace draxul::markdown
```

The key rule: the renderer never assumes rows are equal height. Every visible row is located by pixel scroll offset.

---

## Task 1: Add Markdown Host Kind and Build Skeleton

**Files:**
- Modify: `libs/draxul-types/include/draxul/host_kind.h`
- Create: `libs/draxul-markdown/CMakeLists.txt`
- Create: `libs/draxul-markdown/include/draxul/markdown/markdown_host.h`
- Create: `libs/draxul-markdown/src/markdown_host.cpp`
- Modify: root `CMakeLists.txt`
- Modify: `app/main.cpp`
- Modify: `app/command_palette.cpp`

- [ ] **Step 1: Add `HostKind::Markdown`**

Add `Markdown` to the enum and parse/format `"markdown"`.

Expected behavior:

```cpp
REQUIRE(parse_host_kind("markdown") == HostKind::Markdown);
REQUIRE(std::string(to_string(HostKind::Markdown)) == "markdown");
```

- [ ] **Step 2: Create a compiling `MarkdownHost` stub**

The initial host should implement `IHost` directly, not `GridHostBase`.

Required behavior:

```cpp
class MarkdownHost final : public IHost
{
public:
    bool initialize(const HostContext& context, IHostCallbacks& callbacks) override;
    void shutdown() override;
    bool is_running() const override;
    std::string init_error() const override;
    void set_viewport(const HostViewport& viewport) override;
    void pump() override;
    void draw(IFrameContext& frame) override;
    std::optional<std::chrono::steady_clock::time_point> next_deadline() const override;
    void on_mouse_wheel(const MouseWheelEvent& event) override;
    void on_key(const KeyEvent& event) override;
    bool dispatch_action(std::string_view action) override;
    void request_close() override;
    std::string status_text() const override;
    Color default_background() const override;
};
```

- [ ] **Step 3: Register the host**

Add a provider function:

```cpp
std::unique_ptr<IHost> create_markdown_host();
void register_markdown_host_provider(HostProviderRegistry& registry);
```

Register it with `HostProviderRegistry` from `app/main.cpp` so `--host markdown` can create it.

- [ ] **Step 4: Add a smoke test**

Create `tests/markdown_host_tests.cpp` with a test that creates the host through the registry and checks it reports an error when no source file is supplied.

Run:

```powershell
cmake --build build --config Release --target draxul-tests
ctest --test-dir build --build-config Release -R draxul-tests --output-on-failure
```

Expected: test builds and passes.

---

## Task 2: Add Markdown Parser Dependency and Document Model

**Files:**
- Modify: `cmake/FetchDependencies.cmake`
- Modify: root `CMakeLists.txt`
- Modify: `libs/draxul-markdown/CMakeLists.txt`
- Create: `libs/draxul-markdown/include/draxul/markdown/markdown_document.h`
- Create: `libs/draxul-markdown/include/draxul/markdown/markdown_parser.h`
- Create: `libs/draxul-markdown/src/markdown_document.cpp`
- Create: `libs/draxul-markdown/src/markdown_parser_md4c.cpp`
- Create: `tests/markdown_parser_tests.cpp`

- [ ] **Step 1: Fetch MD4C**

Add MD4C with `FetchContent`. Use the C parser library; do not use the HTML renderer.

Candidate CMake:

```cmake
FetchContent_Declare(
    md4c
    GIT_REPOSITORY https://github.com/mity/md4c.git
    GIT_TAG release-0.5.2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(md4c)
```

If upstream target names differ after configure, adapt `libs/draxul-markdown/CMakeLists.txt` to link against the generated parser target only.

- [ ] **Step 2: Define parser API**

Public API:

```cpp
namespace draxul::markdown
{

struct ParseOptions
{
    bool enable_tables = true;
    bool enable_task_lists = true;
    bool enable_strikethrough = true;
    bool enable_wikilinks = true;
    bool enable_callouts = true;
};

struct ParseResult
{
    Document document;
    bool ok = true;
    std::string error;
};

ParseResult parse_markdown(std::filesystem::path source_path, std::string source, ParseOptions options = {});

} // namespace draxul::markdown
```

- [ ] **Step 3: Write parser tests first**

Test cases:

```cpp
TEST_CASE("markdown parser builds heading and paragraph blocks", "[markdown][parser]")
{
    auto result = parse_markdown("test.md", "# Title\n\nHello **world**.\n");
    REQUIRE(result.ok);
    REQUIRE(result.document.blocks.size() == 2);
    REQUIRE(result.document.blocks[0].kind == BlockKind::Heading);
    REQUIRE(result.document.blocks[0].heading_level == 1);
    REQUIRE(result.document.blocks[1].kind == BlockKind::Paragraph);
}
```

Also test lists, fenced code blocks, links, inline code, and thematic breaks.

- [ ] **Step 4: Implement MD4C callback adapter**

Build the document AST from MD4C enter/leave/text callbacks. Preserve text and nesting; do not attempt layout here.

- [ ] **Step 5: Run parser tests**

Run:

```powershell
cmake --build build --config Release --target draxul-tests
.\build\Release\draxul-tests.exe [markdown][parser]
```

Expected: all parser tests pass.

---

## Task 3: Front Matter, Callouts, and Obsidian-Style Surface Features

**Files:**
- Create: `libs/draxul-markdown/src/markdown_front_matter.cpp`
- Modify: `libs/draxul-markdown/include/draxul/markdown/markdown_parser.h`
- Modify: `libs/draxul-markdown/src/markdown_parser_md4c.cpp`
- Create: `tests/markdown_front_matter_tests.cpp`

- [ ] **Step 1: Parse YAML front matter before MD4C**

If source starts with `---` on the first line, scan until a closing `---` or `...` line. Remove that range from the text passed to MD4C and prepend a `BlockKind::FrontMatter` block.

Test:

```cpp
TEST_CASE("markdown parser extracts yaml front matter", "[markdown][frontmatter]")
{
    auto result = parse_markdown("note.md", "---\ntitle: Test\ntags: [one, two]\n---\n\n# Body\n");
    REQUIRE(result.ok);
    REQUIRE(result.document.blocks.front().kind == BlockKind::FrontMatter);
    REQUIRE(result.document.blocks.front().front_matter.size() == 2);
    REQUIRE(result.document.blocks.front().front_matter[0].key == "title");
}
```

For MVP, parse simple `key: value` lines and preserve complex values as strings.

- [ ] **Step 2: Detect Obsidian-style callouts**

Transform blockquotes beginning with `[!note]`, `[!warning]`, `[!tip]`, `[!info]`, `[!todo]`, `[!question]`, or `[!example]` into `BlockKind::Callout`.

Test:

```cpp
TEST_CASE("markdown parser recognizes callout blockquotes", "[markdown][callout]")
{
    auto result = parse_markdown("note.md", "> [!warning] Careful\n> Body text\n");
    REQUIRE(result.ok);
    REQUIRE(result.document.blocks[0].kind == BlockKind::Callout);
    REQUIRE(result.document.blocks[0].callout_type == "warning");
    REQUIRE(result.document.blocks[0].callout_title == "Careful");
}
```

- [ ] **Step 3: Tokenize wikilinks as links**

Recognize `[[target]]` and `[[target|label]]` in text nodes and convert them into `InlineKind::Link`.

Test:

```cpp
TEST_CASE("markdown parser recognizes wikilinks", "[markdown][wikilink]")
{
    auto result = parse_markdown("note.md", "See [[Daily Note|today]].\n");
    REQUIRE(result.ok);
    const auto& paragraph = result.document.blocks[0];
    REQUIRE(paragraph.inlines.size() >= 2);
    REQUIRE(std::ranges::any_of(paragraph.inlines, [](const Inline& in) {
        return in.kind == InlineKind::Link && in.destination == "Daily Note";
    }));
}
```

---

## Task 4: Rich Text Service for Multiple Font Sizes

**Files:**
- Create: `libs/draxul-font/include/draxul/rich_text_service.h`
- Create: `libs/draxul-font/src/rich_text_service.cpp`
- Modify: `libs/draxul-font/CMakeLists.txt`
- Create: `tests/rich_text_service_tests.cpp`

- [ ] **Step 1: Define rich text style key and resolved cluster**

Public API:

```cpp
namespace draxul
{

struct RichTextStyleKey
{
    float point_size = TextService::DEFAULT_POINT_SIZE;
    bool bold = false;
    bool italic = false;

    bool operator==(const RichTextStyleKey&) const = default;
};

struct RichTextCluster
{
    AtlasRegion atlas;
    FontMetrics metrics{};
    float advance_px = 0.0f;
};

class RichTextService : public IGlyphAtlas
{
public:
    bool initialize(const TextServiceConfig& config, float base_point_size, float display_ppi);
    void shutdown();
    RichTextCluster resolve_cluster(const std::string& text, const RichTextStyleKey& style);
    const FontMetrics& metrics_for(const RichTextStyleKey& style);

    AtlasRegion resolve_cluster(const std::string& text, bool is_bold, bool is_italic) override;
    int ligature_cell_span(const std::string& text, bool is_bold, bool is_italic) override;
    bool atlas_dirty() const override;
    bool consume_atlas_reset() override;
    void clear_atlas_dirty() override;
    const uint8_t* atlas_data() const override;
    int atlas_width() const override;
    int atlas_height() const override;
    AtlasDirtyRect atlas_dirty_rect() const override;
};

} // namespace draxul
```

- [ ] **Step 2: Implement one atlas shared across point sizes**

Implementation direction:

- Keep a `FontResolver` pool keyed by rounded point size.
- Each resolver owns faces/shapers for that point size.
- A single `GlyphCache` stores atlas regions.
- The glyph cache key must include text, point size, bold, italic, and selected face identity so the same text at 11 pt and 28 pt does not collide.
- Do not mutate the existing `TextService` behavior.

- [ ] **Step 3: Add tests**

Test cases:

```cpp
TEST_CASE("RichTextService resolves same text at different sizes independently", "[font][richtext]")
{
    RichTextService service;
    REQUIRE(service.initialize(TextServiceConfig{}, 11.0f, 96.0f));
    auto small = service.resolve_cluster("Heading", RichTextStyleKey{ .point_size = 11.0f });
    auto large = service.resolve_cluster("Heading", RichTextStyleKey{ .point_size = 28.0f, .bold = true });
    REQUIRE(small.atlas.size.y > 0);
    REQUIRE(large.atlas.size.y > small.atlas.size.y);
    service.shutdown();
}
```

Run:

```powershell
cmake --build build --config Release --target draxul-tests
.\build\Release\draxul-tests.exe [font][richtext]
```

Expected: tests pass without affecting existing `[font]` tests.

---

## Task 5: Markdown Theme and Style Mapping

**Files:**
- Create: `libs/draxul-markdown/include/draxul/markdown/markdown_theme.h`
- Create: `libs/draxul-markdown/src/markdown_theme.cpp`
- Create: `tests/markdown_layout_tests.cpp`

- [ ] **Step 1: Define theme styles**

The theme maps document semantics to point sizes, weights, colors, margins, and line heights.

```cpp
struct MarkdownTextStyle
{
    RichTextStyleKey font;
    Color color;
    Color background;
    float line_height = 0.0f;
    float paragraph_spacing_after = 0.0f;
};

struct MarkdownTheme
{
    Color page_background;
    Color text;
    Color muted;
    Color accent;
    Color code_background;
    Color callout_background;
    Color quote_bar;
    float content_margin_x = 48.0f;
    float content_margin_y = 36.0f;
    float max_content_width = 860.0f;
    MarkdownTextStyle body;
    MarkdownTextStyle heading1;
    MarkdownTextStyle heading2;
    MarkdownTextStyle heading3;
    MarkdownTextStyle heading4;
    MarkdownTextStyle heading5;
    MarkdownTextStyle heading6;
    MarkdownTextStyle code;
};
```

- [ ] **Step 2: Add default dark theme**

Use Draxul-style dark colors, not Obsidian copies. Headings should visibly scale:

- Body: base font size.
- H1: 2.0x base, bold.
- H2: 1.6x base, bold.
- H3: 1.35x base, bold.
- H4-H6: smaller stepped sizes.

- [ ] **Step 3: Test heading style sizes**

Test that `style_for_heading(1)` has a larger `point_size` than body and H2.

---

## Task 6: Variable-Row Layout Engine

**Files:**
- Create: `libs/draxul-markdown/include/draxul/markdown/markdown_layout.h`
- Create: `libs/draxul-markdown/src/markdown_layout.cpp`
- Modify: `tests/markdown_layout_tests.cpp`

- [ ] **Step 1: Define layout API**

```cpp
struct LayoutOptions
{
    float viewport_width = 1280.0f;
    float viewport_height = 720.0f;
    float pixel_scale = 1.0f;
};

LayoutDocument layout_markdown_document(
    const Document& document,
    const MarkdownTheme& theme,
    RichTextService& text,
    const LayoutOptions& options);
```

- [ ] **Step 2: Implement paragraph wrapping**

Wrap text runs to `content_width`. Use shaped cluster advance from `RichTextService`; do not use character count. MVP can tokenize on whitespace and inline boundaries.

Required behavior:

- Long paragraphs produce multiple `LayoutRow` entries.
- Every row has positive `height`.
- `content_height` equals the bottom of the final row plus bottom margin.

- [ ] **Step 3: Implement headings as larger variable rows**

Headings use the style for their heading level. Their row height comes from `metrics_for(style)` and theme spacing, not terminal cell size.

Test:

```cpp
TEST_CASE("markdown layout gives h1 a taller row than body text", "[markdown][layout]")
{
    auto parsed = parse_markdown("test.md", "# Big\n\nBody text\n");
    RichTextService text;
    REQUIRE(text.initialize(TextServiceConfig{}, 11.0f, 96.0f));
    auto layout = layout_markdown_document(parsed.document, default_markdown_theme(11.0f), text, {});
    REQUIRE(layout.rows.size() >= 2);
    REQUIRE(layout.rows[0].height > layout.rows[1].height);
    text.shutdown();
}
```

- [ ] **Step 4: Add front matter, code block, list, quote, and callout layout**

For MVP:

- Front matter: draw a metadata panel with rows for `key: value`.
- Code block: monospace-looking style from current font, background rect, preserve line breaks.
- List: bullet or number decoration plus child text rows.
- Task list: checkbox decoration plus child text rows.
- Quote/callout: left bar/background decoration plus child text rows.
- Table: simple column layout; clip long text, do not implement complex table wrapping in MVP.

---

## Task 7: Pixel Scroll Model and Visible Row Culling

**Files:**
- Create: `libs/draxul-markdown/include/draxul/markdown/markdown_scroll.h`
- Create: `libs/draxul-markdown/src/markdown_scroll.cpp`
- Create: `tests/markdown_scroll_tests.cpp`

- [ ] **Step 1: Define scroll state**

```cpp
class MarkdownScrollState
{
public:
    void set_viewport_height(float height);
    void set_content_height(float height);
    void scroll_pixels(float delta);
    void page_down();
    void page_up();
    void home();
    void end();
    float offset() const;
    float max_offset() const;
    bool scrollbar_visible() const;
    Rect scrollbar_track(float x, float y, float width, float height) const;
    Rect scrollbar_thumb(float x, float y, float width, float height) const;
};
```

- [ ] **Step 2: Add clamp tests**

Test that scrolling cannot go below zero or past `content_height - viewport_height`.

- [ ] **Step 3: Add visible row query**

Add:

```cpp
std::span<const LayoutRow> visible_rows(const LayoutDocument& layout, float scroll_offset, float viewport_height);
```

Use binary search over row `y` values so large markdown files do not require scanning the whole document every frame.

---

## Task 8: Rich Text Draw List and Renderer Pass

**Files:**
- Create: `libs/draxul-markdown/src/markdown_render_pass.h`
- Create: `libs/draxul-markdown/src/markdown_render_pass_vk.cpp`
- Create: `libs/draxul-markdown/src/markdown_render_pass_metal.mm`
- Create: `libs/draxul-markdown/src/markdown_render_pass_null.cpp`
- Create: `shaders/markdown_text.glsl`
- Create: `shaders/markdown_text.metal`

- [ ] **Step 1: Define draw instances**

```cpp
struct MarkdownGlyphInstance
{
    glm::vec2 pos;
    glm::vec2 size;
    glm::vec4 uv;
    glm::vec4 color;
    uint32_t flags = 0;
};

struct MarkdownRectInstance
{
    glm::vec2 pos;
    glm::vec2 size;
    glm::vec4 color;
    float radius = 0.0f;
};

struct MarkdownDrawList
{
    std::vector<MarkdownRectInstance> rects;
    std::vector<MarkdownGlyphInstance> glyphs;
};
```

- [ ] **Step 2: Build draw list from visible rows**

Convert `LayoutRow` runs and decorations into draw instances, applying:

- pane viewport offset
- document margin
- negative scroll offset
- clipping to pane viewport

- [ ] **Step 3: Implement Vulkan render pass**

The pass owns:

- atlas image/view/sampler
- glyph instance buffer
- rect instance buffer
- pipeline layout and pipelines

Record sequence:

1. Upload dirty atlas regions from `RichTextService`.
2. Upload instance buffers.
3. Draw rect instances.
4. Draw glyph instances.

- [ ] **Step 4: Implement Metal render pass**

Mirror Vulkan behavior with Metal buffers, atlas texture, and pipelines.

- [ ] **Step 5: Add null render pass**

For tests or unsupported builds, a null pass should accept draw lists and report no init error but draw nothing.

---

## Task 9: MarkdownHost File Loading, Layout, and Rendering

**Files:**
- Modify: `libs/draxul-markdown/include/draxul/markdown/markdown_host.h`
- Modify: `libs/draxul-markdown/src/markdown_host.cpp`

- [ ] **Step 1: Load source file in `initialize()`**

Rules:

- If `launch_options.source_path` is empty, set `init_error_ = "Markdown host requires --source <file>"`.
- If the path does not exist, set `init_error_ = "Markdown source path does not exist: <path>"`.
- If the path is a directory, set `init_error_ = "Markdown source path is not a file: <path>"`.
- If read fails, set `init_error_` with the filesystem error.

- [ ] **Step 2: Parse and layout**

Pipeline:

```cpp
source_text_ = read_file(path);
parse_result_ = parse_markdown(path, source_text_);
theme_ = default_markdown_theme(context.text_service ? context.text_service->point_size() : 11.0f);
rich_text_.initialize(font_config_from_context_or_default, theme_.body.font.point_size, context.display_ppi);
layout_ = layout_markdown_document(parse_result_.document, theme_, rich_text_, layout_options_from_viewport());
scroll_.set_content_height(layout_.content_height);
```

Because `HostContext` currently exposes `TextService*` but not the original `TextServiceConfig`, this task should either:

- add a narrow method to `TextService` exposing its config, or
- initialize `RichTextService` with `TextServiceConfig{ .font_path = context.text_service->primary_font_path() }` and current fallbacks.

Prefer the smallest public API that avoids duplicating font configuration.

- [ ] **Step 3: Render on `draw()`**

In `draw()`:

1. Build/update draw list when layout, scroll, atlas, or viewport changes.
2. Pass draw list and atlas service to `MarkdownRenderPass`.
3. Call `frame.record_render_pass(*render_pass_, viewport)`.
4. Call `frame.flush_submit_chunk()`.

- [ ] **Step 4: Handle resize**

On `set_viewport()`:

- update `viewport_`
- update layout width/height
- preserve scroll offset if possible
- mark draw list dirty

- [ ] **Step 5: Handle scroll input**

On mouse wheel:

```cpp
scroll_.scroll_pixels(-event.y * 56.0f * scroll_speed);
mark_dirty();
```

Use config `scroll_speed` if available. Do not implement kinetic scrolling in MVP.

- [ ] **Step 6: Handle keyboard navigation**

Support:

- `PageDown`
- `PageUp`
- `Home`
- `End`
- arrow up/down as small pixel scrolls

---

## Task 10: Error and Empty States

**Files:**
- Modify: `libs/draxul-markdown/src/markdown_host.cpp`
- Modify: `libs/draxul-markdown/src/markdown_layout.cpp`
- Modify: `tests/markdown_host_tests.cpp`

- [ ] **Step 1: Render errors as markdown viewer content**

Instead of a blank pane, build a synthetic document:

```markdown
# Markdown viewer error

<error text>
```

This keeps rendering code identical for success and failure states.

- [ ] **Step 2: Render empty markdown files**

An empty file should show a subtle title/status row:

```markdown
# Empty markdown file
```

Do not treat empty files as errors.

---

## Task 11: CLI, Command Palette, Docs, and Feature Listing

**Files:**
- Modify: `app/cli_args.cpp` only if help text has a host list.
- Modify: `app/command_palette.cpp`
- Modify: `docs/features.md`

- [ ] **Step 1: Add command palette entry**

Add `Markdown` as a selectable host kind if the command palette lists host types.

- [ ] **Step 2: Update features doc**

Add a feature entry:

```markdown
- Markdown viewer host (`--host markdown --source <file>`) renders markdown files with native Draxul font shaping, variable-size headings, front matter, lists, code blocks, callouts, and pixel scrolling.
```

- [ ] **Step 3: Add usage example**

Document:

```powershell
.\build\Release\draxul.exe --host markdown --source README.md
```

---

## Task 12: Render Validation and Smoke Testing

**Files:**
- Add render fixture markdown files under an existing test fixture directory if one exists; otherwise create `tests/fixtures/markdown/`.
- Modify render/smoke tests only after checking existing render test patterns.

- [ ] **Step 1: Add fixture markdown files**

Fixtures:

- `basic.md`: heading, paragraph, list.
- `frontmatter.md`: YAML metadata and body.
- `large.md`: enough rows to require scrollbar.
- `callout-code.md`: callout plus fenced code.

- [ ] **Step 2: Add a render/snapshot scenario**

Use existing render test support if possible. The scenario should assert:

- H1 row is taller than body row.
- Scrollbar appears for `large.md`.
- No blank frame is produced.

- [ ] **Step 3: Run required validation**

Minimum before review:

```powershell
cmake --build build --config Release --target draxul draxul-tests
ctest --test-dir build --build-config Release --output-on-failure
python do.py smoke
```

If renderer code changes:

```powershell
t.bat
```

If render references need blessing, use the repo's specific bless commands only after visual review.

---

## Agent Work Packages

Use these packages when dispatching agents. Keep write sets disjoint.

### Agent A: Parser and Document Model

Owns:

- `libs/draxul-markdown/include/draxul/markdown/markdown_document.h`
- `libs/draxul-markdown/include/draxul/markdown/markdown_parser.h`
- `libs/draxul-markdown/src/markdown_document.cpp`
- `libs/draxul-markdown/src/markdown_parser_md4c.cpp`
- `libs/draxul-markdown/src/markdown_front_matter.cpp`
- `tests/markdown_parser_tests.cpp`
- `tests/markdown_front_matter_tests.cpp`
- MD4C CMake wiring

Deliverable: parser tests passing.

### Agent B: Rich Text Service

Owns:

- `libs/draxul-font/include/draxul/rich_text_service.h`
- `libs/draxul-font/src/rich_text_service.cpp`
- `libs/draxul-font/CMakeLists.txt`
- `tests/rich_text_service_tests.cpp`

Deliverable: variable-size glyph resolution in one rich atlas without regressing existing font tests.

### Agent C: Theme, Layout, and Scroll

Owns:

- `libs/draxul-markdown/include/draxul/markdown/markdown_theme.h`
- `libs/draxul-markdown/include/draxul/markdown/markdown_layout.h`
- `libs/draxul-markdown/include/draxul/markdown/markdown_scroll.h`
- `libs/draxul-markdown/src/markdown_theme.cpp`
- `libs/draxul-markdown/src/markdown_layout.cpp`
- `libs/draxul-markdown/src/markdown_scroll.cpp`
- `tests/markdown_layout_tests.cpp`
- `tests/markdown_scroll_tests.cpp`

Deliverable: variable-height rows, heading sizing, wrapping, and scrollbar math passing tests.

### Agent D: Vulkan Render Pass

Owns:

- `libs/draxul-markdown/src/markdown_render_pass.h`
- `libs/draxul-markdown/src/markdown_render_pass_vk.cpp`
- `shaders/markdown_text.glsl`
- Vulkan-specific CMake/shader compile changes

Deliverable: Vulkan path draws rects and glyphs from a `MarkdownDrawList`.

### Agent E: Metal Render Pass

Owns:

- `libs/draxul-markdown/src/markdown_render_pass_metal.mm`
- `shaders/markdown_text.metal`
- macOS-specific CMake/shader packaging changes

Deliverable: Metal path matches Vulkan draw-list semantics.

### Agent F: Host Integration and Docs

Owns:

- `libs/draxul-markdown/include/draxul/markdown/markdown_host.h`
- `libs/draxul-markdown/src/markdown_host.cpp`
- `libs/draxul-markdown/CMakeLists.txt`
- `libs/draxul-types/include/draxul/host_kind.h`
- `app/main.cpp`
- `app/command_palette.cpp`
- `tests/markdown_host_tests.cpp`
- `docs/features.md`

Deliverable: `--host markdown --source <file>` starts the viewer host.

## Recommended Execution Order

1. Agent A and Agent B can run in parallel.
2. Agent C starts after Agent A has the document model API and can use a stub `RichTextService` if Agent B is still running.
3. Agent D and Agent E start after the draw-list structs are stable.
4. Agent F starts with a stub/null render pass, then integrates real render passes after D/E land.
5. Main agent reviews and integrates after each package, running targeted tests before dispatching dependent work.

## Risks and Mitigations

- **Risk: Multi-size atlas complicates existing terminal font code.** Mitigation: introduce `RichTextService` separately and keep `TextService` unchanged.
- **Risk: Renderer pass duplicates grid atlas upload code.** Mitigation: reuse `PendingAtlasUpload` helpers and keep markdown atlas ownership private to the markdown render pass.
- **Risk: MD4C source mapping is not enough for future editing.** Mitigation: parser abstraction and `SourceSpan` fields exist now; later editor work can add tree-sitter-markdown or a source-map pass without changing layout/rendering.
- **Risk: Metal/Vulkan drift.** Mitigation: shared `MarkdownDrawList` structs and matching shader inputs; keep backend code limited to upload and draw commands.
- **Risk: Large markdown files relayout too often.** Mitigation: only relayout on source load, viewport width change, theme/font change, or config reload. Scroll only rebuilds visible draw lists.

## Self-Review Checklist

- Parser and document model are separate from layout and rendering.
- Variable-height rows are present from day one.
- The terminal grid renderer is not used.
- Markdown host derives from `IHost`, not `GridHostBase`.
- Headings use larger point sizes through `RichTextService`.
- Initial implementation is viewer-only.
- Source file loading uses existing `HostLaunchOptions::source_path`.
- Scrollbar is included in MVP.
- Agent packages have disjoint write ownership.
- Validation includes build, tests, smoke, and render checks.

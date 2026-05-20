# Markdown GPU Draw-List Renderer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the markdown viewer's CPU-composited `MarkdownBitmap` path with a native GPU draw-list render pass that preserves all current markdown viewer features.

**Architecture:** Keep the existing markdown parser, layout, theme, scroll state, and host lifecycle. Replace `paint_markdown_bitmap()` with a draw-list builder that emits visible rect and glyph instances, grouped by rich-text atlas id. The platform render passes upload instance buffers and rich-text atlas updates, then draw decorations and glyph quads directly through Vulkan or Metal.

**Tech Stack:** C++20, Draxul `IRenderPass`, FreeType/HarfBuzz via `RichTextService`, Vulkan/VMA on Windows, Metal on macOS, Catch2 tests, existing CMake shader compilation.

---

## Design Decisions

- No runtime fallback: once the GPU draw-list path is implemented and verified on the active platform, remove `markdown_bitmap.*` from the library and tests.
- Full parity in the first replacement: front matter panels, headings, paragraphs, code backgrounds, quote/callout backgrounds and left bars, list bullets, task checkboxes, dividers, scrollbar track/thumb, PageUp/PageDown/Home/End, resize, and font-size reload all remain working.
- Do not use the terminal grid for markdown. Markdown remains an `IHost` that records a custom `IRenderPass`.
- Use Draxul font technology directly. Glyphs/clusters still come from `RichTextService`; the difference is that markdown now emits glyph quads that sample rich-text atlases on the GPU.
- Keep render-pass state platform-local. Shared markdown code produces plain C++ draw data and atlas upload payloads; Vulkan and Metal own their buffers, textures, descriptors, and pipelines.

## File Structure

### Create

- `libs/draxul-markdown/include/draxul/markdown/markdown_draw_list.h`
  - Public CPU-side instance structs and builder options/results.
- `libs/draxul-markdown/src/markdown_draw_list.cpp`
  - Visible-row traversal, decoration conversion, safe text chunking, glyph quad emission, batch grouping.
- `tests/markdown_draw_list_tests.cpp`
  - Unit tests for draw-list parity and atlas-safe text handling.
- `shaders/markdown_rect.vert`
  - Vulkan rect instance vertex shader.
- `shaders/markdown_rect.frag`
  - Vulkan rect fragment shader.
- `shaders/markdown_glyph.vert`
  - Vulkan glyph instance vertex shader.
- `shaders/markdown_glyph.frag`
  - Vulkan glyph fragment shader.
- `shaders/markdown.metal`
  - Metal rect and glyph shader functions.

### Modify

- `libs/draxul-font/include/draxul/rich_text_service.h`
  - Add atlas ids, atlas generations, atlas snapshots, and dirty/reset queries across all style services.
- `libs/draxul-font/src/rich_text_service.cpp`
  - Implement stable atlas ids and snapshot extraction.
- `libs/draxul-markdown/include/draxul/markdown/markdown_render_pass.h`
  - Replace bitmap API with draw-list and atlas-upload API.
- `libs/draxul-markdown/src/markdown_render_pass.cpp`
  - Store `MarkdownDrawList`, pending atlas uploads, and revision counters.
- `libs/draxul-markdown/src/markdown_render_pass_vk.cpp`
  - Replace viewport texture upload with rect/glyph instance buffers, atlas textures, descriptors, and draw calls.
- `libs/draxul-markdown/src/markdown_render_pass_metal.mm`
  - Replace viewport texture upload with Metal buffers, atlas textures, and draw calls.
- `libs/draxul-markdown/include/draxul/markdown/markdown_host.h`
  - Rename paint dirty state to draw-list dirty state; remove bitmap dependency.
- `libs/draxul-markdown/src/markdown_host.cpp`
  - Build draw lists and atlas upload payloads instead of painting a bitmap.
- `libs/draxul-markdown/CMakeLists.txt`
  - Add draw-list source; remove bitmap source after migration.
- `cmake/CompileShaders_Metal.cmake`
  - Compile and link `markdown.metal`.
- `CMakeLists.txt`
  - Stage `markdown.metallib` on macOS.
- `tests/CMakeLists.txt`
  - Auto-discovery already picks up the new test; no source-list edit should be needed.
- `docs/features.md`
  - Update markdown viewer implementation note from CPU bitmap to GPU draw list.

### Delete

- `libs/draxul-markdown/include/draxul/markdown/markdown_bitmap.h`
- `libs/draxul-markdown/src/markdown_bitmap.cpp`
- `tests/markdown_bitmap_tests.cpp`

---

## Task 1: Extend RichTextService With Stable Atlas Snapshots

**Files:**
- Modify: `libs/draxul-font/include/draxul/rich_text_service.h`
- Modify: `libs/draxul-font/src/rich_text_service.cpp`
- Test: `tests/rich_text_service_tests.cpp`

- [ ] **Step 1: Add failing tests for atlas ids and snapshots**

Add this test to `tests/rich_text_service_tests.cpp`:

```cpp
TEST_CASE("rich text service exposes stable atlas snapshots per style", "[font][richtext]")
{
    auto service = make_initialized_service();

    RichTextStyleKey body{ .point_size = 12.0f };
    RichTextStyleKey heading{ .point_size = 24.0f, .bold = true };

    const auto body_a = service.resolve_cluster("Body", body);
    const auto body_b = service.resolve_cluster("More", body);
    const auto heading_a = service.resolve_cluster("Heading", heading);

    REQUIRE(body_a.atlas_id == body_b.atlas_id);
    REQUIRE(body_a.atlas_id != heading_a.atlas_id);
    REQUIRE(body_a.atlas_generation > 0);
    REQUIRE(heading_a.atlas_generation > 0);

    const auto snapshots = service.atlas_snapshots();
    REQUIRE(snapshots.size() >= 2);

    const auto body_it = std::ranges::find(
        snapshots,
        body_a.atlas_id,
        &RichTextAtlasSnapshot::atlas_id);
    REQUIRE(body_it != snapshots.end());
    REQUIRE(body_it->data != nullptr);
    REQUIRE(body_it->width > 0);
    REQUIRE(body_it->height > 0);
    REQUIRE(body_it->dirty);
    REQUIRE(body_it->dirty_rect.size.x > 0);
    REQUIRE(body_it->dirty_rect.size.y > 0);

    service.clear_atlas_dirty(body_a.atlas_id);
    const auto clean = service.atlas_snapshot(body_a.atlas_id);
    REQUIRE(clean.has_value());
    REQUIRE_FALSE(clean->dirty);

    service.shutdown();
}
```

- [ ] **Step 2: Run the targeted test and verify it fails to compile**

Run:

```powershell
cmake --build build --config Release --target draxul-tests
```

Expected: compile fails because `RichTextCluster::atlas_id`, `RichTextCluster::atlas_generation`, `RichTextAtlasSnapshot`, `atlas_snapshots()`, `atlas_snapshot()`, and `clear_atlas_dirty(uint32_t)` do not exist.

- [ ] **Step 3: Add public snapshot types and APIs**

In `libs/draxul-font/include/draxul/rich_text_service.h`, add:

```cpp
using RichTextAtlasId = uint32_t;

struct RichTextAtlasSnapshot
{
    RichTextAtlasId atlas_id = 0;
    uint32_t generation = 0;
    const uint8_t* data = nullptr;
    int width = 0;
    int height = 0;
    AtlasDirtyRect dirty_rect = {};
    bool dirty = false;
    bool reset_pending = false;
};
```

Extend `RichTextCluster`:

```cpp
struct RichTextCluster
{
    AtlasRegion atlas;
    FontMetrics metrics{};
    float advance_px = 0.0f;
    RichTextAtlasId atlas_id = 0;
    uint32_t atlas_generation = 0;
};
```

Add methods to `RichTextService`:

```cpp
std::vector<RichTextAtlasSnapshot> atlas_snapshots() const;
std::optional<RichTextAtlasSnapshot> atlas_snapshot(RichTextAtlasId atlas_id) const;
bool consume_any_atlas_reset();
void clear_atlas_dirty(RichTextAtlasId atlas_id);
void clear_all_atlas_dirty();
```

Also add `#include <cstdint>`, `#include <optional>`, and `#include <vector>`.

- [ ] **Step 4: Implement atlas ids and generations**

In `RichTextService::Impl`, replace the map value with a style service record:

```cpp
struct StyleService
{
    RichTextAtlasId atlas_id = 0;
    uint32_t generation = 1;
    bool reset_pending = false;
    std::unique_ptr<TextService> service;
};

RichTextAtlasId next_atlas_id = 1;
std::map<RichTextStyleKey, StyleService, RichTextStyleKeyLess> services;
```

Update `service_for()` so new styles allocate `atlas_id = next_atlas_id++`. Update helpers that previously returned `TextService*` so they can also find the owning `StyleService`.

When `resolve_cluster()` calls the backing `TextService`, call `service->consume_atlas_reset()` after resolving. If it returns true, increment the style record generation and mark `reset_pending = true`. Return the style record's atlas id and generation in `RichTextCluster`.

- [ ] **Step 5: Implement snapshot APIs**

Implement snapshots by iterating all style records:

```cpp
std::vector<RichTextAtlasSnapshot> RichTextService::atlas_snapshots() const
{
    std::vector<RichTextAtlasSnapshot> snapshots;
    if (!impl_->initialized)
        return snapshots;

    for (const auto& [style, record] : impl_->services)
    {
        const auto* service = record.service.get();
        if (service == nullptr)
            continue;
        snapshots.push_back(RichTextAtlasSnapshot{
            .atlas_id = record.atlas_id,
            .generation = record.generation,
            .data = service->atlas_data(),
            .width = service->atlas_width(),
            .height = service->atlas_height(),
            .dirty_rect = service->atlas_dirty_rect(),
            .dirty = service->atlas_dirty(),
            .reset_pending = record.reset_pending,
        });
    }
    return snapshots;
}
```

Implement `atlas_snapshot()` by scanning `atlas_snapshots()`. Implement `clear_atlas_dirty(id)` by clearing the matching backing `TextService` and setting `record.reset_pending = false`. Implement `clear_all_atlas_dirty()` by clearing every record. Implement `consume_any_atlas_reset()` by returning true if any record has `reset_pending`, then clearing those flags.

- [ ] **Step 6: Run targeted rich text tests**

Run:

```powershell
cmake --build build --config Release --target draxul-tests
.\build\tests\Release\draxul-tests.exe "[font][richtext]"
```

Expected: all rich-text tests pass.

---

## Task 2: Add Markdown Draw-List Data Types

**Files:**
- Create: `libs/draxul-markdown/include/draxul/markdown/markdown_draw_list.h`
- Modify: `libs/draxul-markdown/CMakeLists.txt`
- Test: `tests/markdown_draw_list_tests.cpp`

- [ ] **Step 1: Add compile-time draw-list tests**

Create `tests/markdown_draw_list_tests.cpp` with:

```cpp
#include <catch2/catch_all.hpp>

#include <draxul/markdown/markdown_draw_list.h>

using namespace draxul;
using namespace draxul::markdown;

TEST_CASE("markdown draw-list instance structs have stable GPU-friendly layouts", "[markdown][drawlist]")
{
    REQUIRE(sizeof(MarkdownRectInstance) == 32);
    REQUIRE(sizeof(MarkdownGlyphInstance) == 64);
    REQUIRE(alignof(MarkdownRectInstance) == 16);
    REQUIRE(alignof(MarkdownGlyphInstance) == 16);
}

TEST_CASE("markdown draw-list reports visible work", "[markdown][drawlist]")
{
    MarkdownDrawList list;
    REQUIRE_FALSE(list.has_work());

    list.rects.push_back(MarkdownRectInstance{
        .rect = glm::vec4(0.0f, 0.0f, 10.0f, 10.0f),
        .color = Color(1.0f, 0.0f, 0.0f, 1.0f),
    });
    REQUIRE(list.has_work());
}
```

- [ ] **Step 2: Run the test and verify it fails to compile**

Run:

```powershell
cmake --build build --config Release --target draxul-tests
```

Expected: compile fails because `markdown_draw_list.h` does not exist.

- [ ] **Step 3: Create the public draw-list header**

Create `libs/draxul-markdown/include/draxul/markdown/markdown_draw_list.h`:

```cpp
#pragma once

#include <draxul/markdown/markdown_layout.h>
#include <draxul/rich_text_service.h>
#include <draxul/types.h>

#include <cstdint>
#include <vector>

namespace draxul::markdown
{

struct alignas(16) MarkdownRectInstance
{
    glm::vec4 rect = {};  // x, y, width, height in viewport-local physical pixels
    glm::vec4 color = {};
};

struct alignas(16) MarkdownGlyphInstance
{
    glm::vec4 rect = {};  // x, y, width, height in viewport-local physical pixels
    glm::vec4 uv = {};
    glm::vec4 color = {};
    uint32_t flags = 0;
    uint32_t atlas_id = 0;
    uint32_t atlas_generation = 0;
    uint32_t _pad = 0;
};

static_assert(sizeof(MarkdownRectInstance) == 32);
static_assert(sizeof(MarkdownGlyphInstance) == 64);

struct MarkdownGlyphBatch
{
    uint32_t atlas_id = 0;
    uint32_t atlas_generation = 0;
    uint32_t first = 0;
    uint32_t count = 0;
};

struct MarkdownAtlasUpload
{
    uint32_t atlas_id = 0;
    uint32_t generation = 0;
    uint64_t upload_revision = 0;
    int atlas_width = 0;
    int atlas_height = 0;
    AtlasDirtyRect rect = {};
    std::vector<uint8_t> rgba;
    bool full_upload = false;
};

struct MarkdownDrawList
{
    std::vector<MarkdownRectInstance> rects;
    std::vector<MarkdownGlyphInstance> glyphs;
    std::vector<MarkdownGlyphBatch> glyph_batches;
    std::vector<uint32_t> used_atlas_ids;
    Color clear_color = Color(0.0f, 0.0f, 0.0f, 0.0f);
    int viewport_width = 0;
    int viewport_height = 0;

    bool has_work() const
    {
        return !rects.empty() || !glyphs.empty();
    }
};

struct MarkdownDrawListOptions
{
    int viewport_width = 0;
    int viewport_height = 0;
    float scroll_offset = 0.0f;
    float pixel_scale = 1.0f;
};

MarkdownDrawList build_markdown_draw_list(
    const LayoutDocument& document,
    const MarkdownTheme& theme,
    draxul::RichTextService& rich_text,
    const MarkdownDrawListOptions& options);

std::vector<MarkdownAtlasUpload> collect_markdown_atlas_uploads(
    draxul::RichTextService& rich_text,
    std::span<const uint32_t> used_atlas_ids,
    uint64_t upload_revision);

} // namespace draxul::markdown
```

- [ ] **Step 4: Add the source file to CMake**

Create an empty implementation file `libs/draxul-markdown/src/markdown_draw_list.cpp`:

```cpp
#include <draxul/markdown/markdown_draw_list.h>

namespace draxul::markdown
{

MarkdownDrawList build_markdown_draw_list(
    const LayoutDocument&,
    const MarkdownTheme&,
    draxul::RichTextService&,
    const MarkdownDrawListOptions&)
{
    return {};
}

std::vector<MarkdownAtlasUpload> collect_markdown_atlas_uploads(
    draxul::RichTextService&,
    std::span<const uint32_t>,
    uint64_t)
{
    return {};
}

} // namespace draxul::markdown
```

Add `src/markdown_draw_list.cpp` to `draxul-markdown` in `libs/draxul-markdown/CMakeLists.txt`.

- [ ] **Step 5: Run the draw-list struct tests**

Run:

```powershell
cmake --build build --config Release --target draxul-tests
.\build\tests\Release\draxul-tests.exe "[markdown][drawlist]"
```

Expected: both draw-list tests pass.

---

## Task 3: Implement Draw-List Builder With Full Decoration Parity

**Files:**
- Modify: `libs/draxul-markdown/src/markdown_draw_list.cpp`
- Test: `tests/markdown_draw_list_tests.cpp`

- [ ] **Step 1: Add tests for current visual feature parity**

Append these tests to `tests/markdown_draw_list_tests.cpp`:

```cpp
#include <draxul/markdown/markdown_theme.h>

#include <filesystem>
#include <ranges>
#include <string>

namespace
{
std::filesystem::path repo_root()
{
    auto here = std::filesystem::path(__FILE__).parent_path();
    return here.parent_path();
}

TextServiceConfig rich_text_test_config()
{
    TextServiceConfig config;
    config.font_path = (repo_root() / "fonts" / "JetBrainsMonoNerdFont-Regular.ttf").string();
    config.bold_font_path = (repo_root() / "fonts" / "JetBrainsMonoNerdFont-Bold.ttf").string();
    return config;
}

RichTextService make_initialized_service(float base_point_size = 12.0f)
{
    auto config = rich_text_test_config();
    REQUIRE(std::filesystem::exists(config.font_path));
    RichTextService service;
    REQUIRE(service.initialize(config, base_point_size, 96.0f));
    return service;
}
}

TEST_CASE("markdown draw-list emits rects for every current decoration kind", "[markdown][drawlist]")
{
    auto service = make_initialized_service();
    auto theme = default_markdown_theme(12.0f);

    LayoutDocument layout;
    LayoutRow row;
    row.y = 10.0f;
    row.height = 30.0f;
    row.decorations = {
        Decoration{ .kind = Decoration::Kind::Background, .x = 4.0f, .y = 10.0f, .width = 100.0f, .height = 30.0f, .color = Color(1.0f, 0.0f, 0.0f, 1.0f) },
        Decoration{ .kind = Decoration::Kind::BorderLeft, .x = 6.0f, .y = 10.0f, .width = 3.0f, .height = 30.0f, .color = Color(0.0f, 1.0f, 0.0f, 1.0f) },
        Decoration{ .kind = Decoration::Kind::Divider, .x = 4.0f, .y = 25.0f, .width = 100.0f, .height = 1.0f, .color = Color(0.0f, 0.0f, 1.0f, 1.0f) },
        Decoration{ .kind = Decoration::Kind::Bullet, .x = 12.0f, .y = 20.0f, .width = 8.0f, .height = 8.0f, .color = Color(1.0f, 1.0f, 0.0f, 1.0f) },
        Decoration{ .kind = Decoration::Kind::Checkbox, .x = 24.0f, .y = 18.0f, .width = 10.0f, .height = 10.0f, .color = Color(1.0f, 1.0f, 1.0f, 1.0f), .checked = true },
        Decoration{ .kind = Decoration::Kind::ScrollbarThumb, .x = 190.0f, .y = 10.0f, .width = 6.0f, .height = 40.0f, .color = Color(0.5f, 0.5f, 0.5f, 1.0f) },
    };
    layout.rows.push_back(std::move(row));

    const auto list = build_markdown_draw_list(
        layout,
        theme,
        service,
        MarkdownDrawListOptions{ .viewport_width = 200, .viewport_height = 100 });

    REQUIRE(list.rects.size() >= 9);
    REQUIRE(list.glyphs.empty());
    service.shutdown();
}

TEST_CASE("markdown draw-list emits glyph batches grouped by atlas", "[markdown][drawlist]")
{
    auto service = make_initialized_service();
    auto theme = default_markdown_theme(12.0f);

    LayoutDocument layout;
    LayoutRow body;
    body.y = 0.0f;
    body.height = 24.0f;
    body.baseline = 18.0f;
    body.runs.push_back(TextRun{ .text = "Body text", .style = StyleId{ 0 }, .x = 10.0f, .baseline = 18.0f });
    layout.rows.push_back(std::move(body));

    LayoutRow heading;
    heading.y = 30.0f;
    heading.height = 48.0f;
    heading.baseline = 62.0f;
    heading.runs.push_back(TextRun{ .text = "Heading", .style = StyleId{ 1 }, .x = 10.0f, .baseline = 62.0f });
    layout.rows.push_back(std::move(heading));

    const auto list = build_markdown_draw_list(
        layout,
        theme,
        service,
        MarkdownDrawListOptions{ .viewport_width = 400, .viewport_height = 120 });

    REQUIRE_FALSE(list.glyphs.empty());
    REQUIRE(list.glyph_batches.size() >= 2);
    REQUIRE(list.used_atlas_ids.size() >= 2);

    for (const auto& glyph : list.glyphs)
    {
        REQUIRE(glyph.rect.z > 0.0f);
        REQUIRE(glyph.rect.w > 0.0f);
        REQUIRE(glyph.atlas_id != 0);
        REQUIRE(glyph.atlas_generation != 0);
    }

    service.shutdown();
}

TEST_CASE("markdown draw-list clips work to visible rows", "[markdown][drawlist]")
{
    auto service = make_initialized_service();
    auto theme = default_markdown_theme(12.0f);

    LayoutDocument layout;
    for (int i = 0; i < 20; ++i)
    {
        LayoutRow row;
        row.y = static_cast<float>(i * 30);
        row.height = 30.0f;
        row.baseline = row.y + 20.0f;
        row.runs.push_back(TextRun{
            .text = "Visible row " + std::to_string(i),
            .style = StyleId{},
            .x = 10.0f,
            .baseline = row.baseline,
        });
        layout.rows.push_back(std::move(row));
    }

    const auto list = build_markdown_draw_list(
        layout,
        theme,
        service,
        MarkdownDrawListOptions{ .viewport_width = 400, .viewport_height = 60, .scroll_offset = 300.0f });

    REQUIRE_FALSE(list.glyphs.empty());
    for (const auto& glyph : list.glyphs)
    {
        REQUIRE(glyph.rect.y >= -40.0f);
        REQUIRE(glyph.rect.y <= 80.0f);
    }

    service.shutdown();
}
```

- [ ] **Step 2: Run tests and verify builder tests fail**

Run:

```powershell
cmake --build build --config Release --target draxul-tests
.\build\tests\Release\draxul-tests.exe "[markdown][drawlist]"
```

Expected: tests fail because the builder returns an empty list.

- [ ] **Step 3: Implement style lookup and chunk iteration**

In `markdown_draw_list.cpp`, add local style lookup matching the layout/bitmap code:

```cpp
const MarkdownTextStyle& style_for_id(const MarkdownTheme& theme, StyleId style);
```

Add a safe text chunk helper that does not split immediately after combining marks, variation selectors, ZWJ, or emoji modifiers:

```cpp
std::vector<std::string_view> split_text_chunks(std::string_view text)
{
    constexpr int kMaxCodepoints = 8;
    std::vector<std::string_view> chunks;
    size_t chunk_start = 0;
    size_t offset = 0;
    int codepoints = 0;

    while (offset < text.size())
    {
        const size_t before = offset;
        uint32_t cp = 0;
        if (!draxul::utf8_decode_next(text, offset, cp))
            break;

        ++codepoints;
        const bool sticky = draxul::is_width_ignorable(cp) || draxul::is_emoji_modifier(cp) || cp == 0x200D;
        if (codepoints < kMaxCodepoints || sticky)
            continue;

        chunks.emplace_back(text.data() + chunk_start, offset - chunk_start);
        chunk_start = offset;
        codepoints = 0;
    }

    if (chunk_start < text.size())
        chunks.emplace_back(text.data() + chunk_start, text.size() - chunk_start);
    return chunks;
}
```

Include `<draxul/unicode.h>`, `<algorithm>`, `<span>`, `<string>`, and `<unordered_set>`.

- [ ] **Step 4: Implement decoration conversion**

For each visible row decoration, append rect instances using the same geometry as `markdown_bitmap.cpp`:

```cpp
void append_rect(MarkdownDrawList& list, float x, float y, float width, float height, Color color)
{
    if (width <= 0.0f || height <= 0.0f || color.a <= 0.0f)
        return;
    list.rects.push_back(MarkdownRectInstance{
        .rect = glm::vec4(x, y, width, height),
        .color = color,
    });
}
```

Map decoration kinds:

- `Background`, `BorderLeft`, `Divider`, `ScrollbarThumb`: one rect.
- `Bullet`: one square rect using `max(width, height, 3 * pixel_scale)`.
- `Checkbox`: four border rects, plus two check-mark rects when checked.

Subtract `scroll_offset` from each decoration's `y`.

- [ ] **Step 5: Implement glyph emission and batching**

For every visible text run:

```cpp
float pen_x = run.x;
const auto& style = style_for_id(theme, run.style);
for (std::string_view chunk : split_text_chunks(run.text))
{
    const RichTextCluster cluster = rich_text.resolve_cluster(std::string(chunk), style.rich_text);
    const AtlasRegion& region = cluster.atlas;
    if (region.size.x > 0 && region.size.y > 0)
    {
        list.glyphs.push_back(MarkdownGlyphInstance{
            .rect = glm::vec4(
                pen_x + static_cast<float>(region.bearing.x),
                run.baseline - options.scroll_offset - static_cast<float>(region.bearing.y),
                static_cast<float>(region.size.x),
                static_cast<float>(region.size.y)),
            .uv = region.uv,
            .color = style.foreground,
            .flags = region.is_color ? STYLE_FLAG_COLOR_GLYPH : 0u,
            .atlas_id = cluster.atlas_id,
            .atlas_generation = cluster.atlas_generation,
        });
    }
    pen_x += cluster.advance_px;
}
```

After glyph emission, stable-sort glyphs by `(atlas_id, atlas_generation)` and populate `glyph_batches` with contiguous ranges. Populate `used_atlas_ids` with unique atlas ids.

- [ ] **Step 6: Implement atlas upload collection**

`collect_markdown_atlas_uploads()` should:

1. Query `rich_text.atlas_snapshot(id)` for every used atlas id.
2. Skip clean snapshots unless `reset_pending` is true.
3. Copy the full atlas when `reset_pending` is true.
4. Copy only the dirty rectangle otherwise.
5. Clear the atlas dirty flag after copying.

Use this rectangle-copy loop:

```cpp
for (int row = 0; row < rect.size.y; ++row)
{
    const auto* src = snapshot.data
        + ((static_cast<size_t>(rect.pos.y + row) * snapshot.width + rect.pos.x) * 4u);
    upload.rgba.insert(upload.rgba.end(), src, src + static_cast<size_t>(rect.size.x) * 4u);
}
```

- [ ] **Step 7: Run draw-list tests**

Run:

```powershell
cmake --build build --config Release --target draxul-tests
.\build\tests\Release\draxul-tests.exe "[markdown][drawlist]"
```

Expected: all draw-list tests pass.

---

## Task 4: Convert MarkdownHost From Bitmap Paint To Draw-List Build

**Files:**
- Modify: `libs/draxul-markdown/include/draxul/markdown/markdown_host.h`
- Modify: `libs/draxul-markdown/src/markdown_host.cpp`
- Modify: `libs/draxul-markdown/include/draxul/markdown/markdown_render_pass.h`
- Modify: `libs/draxul-markdown/src/markdown_render_pass.cpp`
- Test: existing markdown host compile through `draxul-tests`

- [ ] **Step 1: Change render pass API in the header**

Replace the bitmap include and API in `markdown_render_pass.h`:

```cpp
#include <draxul/markdown/markdown_draw_list.h>

void set_draw_list(
    MarkdownDrawList draw_list,
    std::vector<MarkdownAtlasUpload> atlas_uploads,
    uint64_t draw_revision);
```

Remove `MarkdownBitmap bitmap_`. Add:

```cpp
MarkdownDrawList draw_list_;
std::vector<MarkdownAtlasUpload> atlas_uploads_;
uint64_t draw_revision_ = 0;
```

- [ ] **Step 2: Implement render pass storage**

In `markdown_render_pass.cpp`, replace `set_bitmap()` with:

```cpp
void MarkdownRenderPass::set_draw_list(
    MarkdownDrawList draw_list,
    std::vector<MarkdownAtlasUpload> atlas_uploads,
    uint64_t draw_revision)
{
    draw_list_ = std::move(draw_list);
    atlas_uploads_ = std::move(atlas_uploads);
    draw_revision_ = draw_revision;
}
```

- [ ] **Step 3: Update host state names**

In `markdown_host.h`, replace:

```cpp
void repaint();
void mark_paint_dirty();
bool paint_dirty_ = true;
uint64_t paint_revision_ = 0;
```

with:

```cpp
void rebuild_draw_list();
void mark_draw_list_dirty();
bool draw_list_dirty_ = true;
uint64_t draw_revision_ = 0;
uint64_t atlas_upload_revision_ = 0;
```

- [ ] **Step 4: Update host lifecycle**

In `markdown_host.cpp`:

- `initialize()` calls `rebuild_layout(); rebuild_draw_list();`.
- `pump()` calls `rebuild_layout()` if layout dirty, then `rebuild_draw_list()` if draw-list dirty.
- `mark_layout_dirty()` sets `layout_dirty_ = true` and calls `mark_draw_list_dirty()`.
- `scroll_pixels()` and key scroll handlers call `mark_draw_list_dirty()`.
- `runtime_state()` checks `!layout_dirty_ && !draw_list_dirty_`.

- [ ] **Step 5: Build draw lists in the host**

Replace `repaint()` with:

```cpp
void MarkdownHost::rebuild_draw_list()
{
    if (!render_pass_)
        return;

    auto paint_document = document_with_scrollbar();
    MarkdownDrawList draw_list = build_markdown_draw_list(
        paint_document,
        theme_,
        rich_text_,
        MarkdownDrawListOptions{
            .viewport_width = std::max(1, viewport_.pixel_size.x),
            .viewport_height = std::max(1, viewport_.pixel_size.y),
            .scroll_offset = scroll_.offset(),
            .pixel_scale = viewport_.pixel_scale,
        });

    auto uploads = collect_markdown_atlas_uploads(
        rich_text_,
        draw_list.used_atlas_ids,
        ++atlas_upload_revision_);

    render_pass_->set_draw_list(std::move(draw_list), std::move(uploads), ++draw_revision_);
    draw_list_dirty_ = false;
}
```

- [ ] **Step 6: Run a compile build**

Run:

```powershell
cmake --build build --config Release --target draxul draxul-tests
```

Expected: Vulkan/Metal render pass implementations still fail until Task 5/6 because they reference the old bitmap fields. Host code should no longer reference `MarkdownBitmap` or `paint_markdown_bitmap()`.

---

## Task 5: Add Vulkan Markdown Draw-List Render Pass

**Files:**
- Create: `shaders/markdown_rect.vert`
- Create: `shaders/markdown_rect.frag`
- Create: `shaders/markdown_glyph.vert`
- Create: `shaders/markdown_glyph.frag`
- Modify: `libs/draxul-markdown/src/markdown_render_pass_vk.cpp`

- [ ] **Step 1: Add Vulkan shaders**

Create `shaders/markdown_rect.vert`:

```glsl
#version 450
#extension GL_GOOGLE_include_directive : require
#include "quad_offsets_shared.h"

layout(push_constant) uniform PushConstants {
    float screen_w;
    float screen_h;
    float viewport_x;
    float viewport_y;
} pc;

struct RectInstance {
    vec4 rect;
    vec4 color;
};

layout(set = 0, binding = 0) readonly buffer RectBuffer {
    RectInstance rects[];
};

layout(location = 0) out vec4 frag_color;

void main() {
    RectInstance instance = rects[gl_InstanceIndex];
    vec2 offsets[6] = vec2[](
        vec2(QUAD_OFFSET_0), vec2(QUAD_OFFSET_1), vec2(QUAD_OFFSET_2),
        vec2(QUAD_OFFSET_3), vec2(QUAD_OFFSET_4), vec2(QUAD_OFFSET_5)
    );
    vec2 offset = offsets[gl_VertexIndex];
    vec2 pos = instance.rect.xy + offset * instance.rect.zw + vec2(pc.viewport_x, pc.viewport_y);
    vec2 ndc = (pos / vec2(pc.screen_w, pc.screen_h)) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    frag_color = instance.color;
}
```

Create `shaders/markdown_rect.frag`:

```glsl
#version 450
layout(location = 0) in vec4 frag_color;
layout(location = 0) out vec4 out_color;
void main() {
    out_color = frag_color;
}
```

Create `shaders/markdown_glyph.vert`:

```glsl
#version 450
#extension GL_GOOGLE_include_directive : require
#include "quad_offsets_shared.h"

layout(push_constant) uniform PushConstants {
    float screen_w;
    float screen_h;
    float viewport_x;
    float viewport_y;
} pc;

struct GlyphInstance {
    vec4 rect;
    vec4 uv;
    vec4 color;
    uint flags;
    uint atlas_id;
    uint atlas_generation;
    uint _pad;
};

layout(set = 0, binding = 0) readonly buffer GlyphBuffer {
    GlyphInstance glyphs[];
};

layout(location = 0) out vec2 frag_uv;
layout(location = 1) out vec4 frag_color;
layout(location = 2) flat out uint frag_flags;

void main() {
    GlyphInstance instance = glyphs[gl_InstanceIndex];
    vec2 offsets[6] = vec2[](
        vec2(QUAD_OFFSET_0), vec2(QUAD_OFFSET_1), vec2(QUAD_OFFSET_2),
        vec2(QUAD_OFFSET_3), vec2(QUAD_OFFSET_4), vec2(QUAD_OFFSET_5)
    );
    vec2 offset = offsets[gl_VertexIndex];
    vec2 pos = instance.rect.xy + offset * instance.rect.zw + vec2(pc.viewport_x, pc.viewport_y);
    vec2 ndc = (pos / vec2(pc.screen_w, pc.screen_h)) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    frag_uv = mix(instance.uv.xy, instance.uv.zw, offset);
    frag_color = instance.color;
    frag_flags = instance.flags;
}
```

Create `shaders/markdown_glyph.frag`:

```glsl
#version 450
#include "decoration_constants.glsl"

layout(set = 0, binding = 1) uniform sampler2D atlas;

layout(location = 0) in vec2 frag_uv;
layout(location = 1) in vec4 frag_color;
layout(location = 2) flat in uint frag_flags;
layout(location = 0) out vec4 out_color;

void main() {
    vec4 atlas_sample = texture(atlas, frag_uv);
    float alpha = atlas_sample.a;
    if (alpha < 0.01) discard;
    bool color_glyph = (frag_flags & STYLE_FLAG_COLOR_GLYPH) != 0u;
    out_color = color_glyph ? atlas_sample : vec4(frag_color.rgb, frag_color.a * alpha);
}
```

- [ ] **Step 2: Replace Vulkan bitmap state with instance/atlas state**

In `markdown_render_pass_vk.cpp`, replace image-only `FrameResources` with:

```cpp
struct AtlasTexture
{
    Image image;
    uint32_t generation = 0;
    uint64_t upload_revision = 0;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    bool descriptor_dirty = true;
};

struct FrameResources
{
    Buffer rect_buffer;
    Buffer glyph_buffer;
    uint64_t uploaded_draw_revision = 0;
    std::map<uint32_t, AtlasTexture> atlases;
};
```

Keep `Buffer` and `Image` helpers. Add descriptor layouts:

- rect layout: one storage buffer at binding 0.
- glyph layout: storage buffer binding 0 plus combined image sampler binding 1.

Add separate descriptor pools, pipeline layouts, and pipelines for rect and glyph.

- [ ] **Step 3: Upload instance buffers in prepass**

In `record_prepass()`:

1. Ensure frame resources for `ctx.buffered_frame_count()`.
2. If `draw_revision_` changed for the current frame, upload `draw_list_.rects` to `frame.rect_buffer`.
3. Upload `draw_list_.glyphs` to `frame.glyph_buffer`.
4. For each `atlas_uploads_`, ensure a texture for the current frame and upload either full atlas or dirty rectangle.

Use buffer sizes:

```cpp
const size_t rect_bytes = draw_list_.rects.size() * sizeof(MarkdownRectInstance);
const size_t glyph_bytes = draw_list_.glyphs.size() * sizeof(MarkdownGlyphInstance);
```

- [ ] **Step 4: Record draw calls**

In `record()`:

1. Return if the draw list has no work.
2. Bind rect pipeline and rect descriptor; draw `6 * rect_count` as instanced quads:

```cpp
vkCmdDraw(cmd, 6, static_cast<uint32_t>(draw_list_.rects.size()), 0, 0);
```

3. Bind glyph pipeline once.
4. For every `MarkdownGlyphBatch`, bind that batch's atlas descriptor and draw:

```cpp
vkCmdDraw(cmd, 6, batch.count, 0, batch.first);
```

Set push constants from framebuffer size and pane viewport:

```cpp
PushConstants push{
    .screen_w = static_cast<float>(std::max(1, vk_ctx.width())),
    .screen_h = static_cast<float>(std::max(1, vk_ctx.height())),
    .viewport_x = static_cast<float>(vk_ctx.viewport_x()),
    .viewport_y = static_cast<float>(vk_ctx.viewport_y()),
};
```

- [ ] **Step 5: Build shaders and app**

Run:

```powershell
cmake --build build --config Release --target compile_shaders
cmake --build build --config Release --target draxul draxul-tests
```

Expected: shaders compile, app and tests link on Windows.

---

## Task 6: Add Metal Markdown Draw-List Render Pass

**Files:**
- Create: `shaders/markdown.metal`
- Modify: `cmake/CompileShaders_Metal.cmake`
- Modify: `CMakeLists.txt`
- Modify: `libs/draxul-markdown/src/markdown_render_pass_metal.mm`

- [ ] **Step 1: Add Metal shaders**

Create `shaders/markdown.metal` with Metal equivalents of the Vulkan rect and glyph shaders:

```metal
#include <metal_stdlib>
using namespace metal;

#include "decoration_constants_shared.h"
#include "quad_offsets_shared.h"

struct RectInstance { float4 rect; float4 color; };
struct GlyphInstance { float4 rect; float4 uv; float4 color; uint flags; uint atlas_id; uint atlas_generation; uint _pad; };
struct PushConstants { float screen_w; float screen_h; float viewport_x; float viewport_y; };

struct RectOut { float4 position [[position]]; float4 color; };

vertex RectOut markdown_rect_vertex(
    uint vertex_id [[vertex_id]],
    uint instance_id [[instance_id]],
    device const RectInstance* rects [[buffer(0)]],
    constant PushConstants& pc [[buffer(1)]])
{
    constexpr float2 offsets[6] = {
        float2(QUAD_OFFSET_0), float2(QUAD_OFFSET_1), float2(QUAD_OFFSET_2),
        float2(QUAD_OFFSET_3), float2(QUAD_OFFSET_4), float2(QUAD_OFFSET_5)
    };
    RectInstance instance = rects[instance_id];
    float2 offset = offsets[vertex_id];
    float2 pos = instance.rect.xy + offset * instance.rect.zw + float2(pc.viewport_x, pc.viewport_y);
    float2 ndc = (pos / float2(pc.screen_w, pc.screen_h)) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    return { float4(ndc, 0.0, 1.0), instance.color };
}

fragment float4 markdown_rect_fragment(RectOut in [[stage_in]]) { return in.color; }

struct GlyphOut { float4 position [[position]]; float2 uv; float4 color; uint flags; };

vertex GlyphOut markdown_glyph_vertex(
    uint vertex_id [[vertex_id]],
    uint instance_id [[instance_id]],
    device const GlyphInstance* glyphs [[buffer(0)]],
    constant PushConstants& pc [[buffer(1)]])
{
    constexpr float2 offsets[6] = {
        float2(QUAD_OFFSET_0), float2(QUAD_OFFSET_1), float2(QUAD_OFFSET_2),
        float2(QUAD_OFFSET_3), float2(QUAD_OFFSET_4), float2(QUAD_OFFSET_5)
    };
    GlyphInstance instance = glyphs[instance_id];
    float2 offset = offsets[vertex_id];
    float2 pos = instance.rect.xy + offset * instance.rect.zw + float2(pc.viewport_x, pc.viewport_y);
    float2 ndc = (pos / float2(pc.screen_w, pc.screen_h)) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    GlyphOut out;
    out.position = float4(ndc, 0.0, 1.0);
    out.uv = mix(instance.uv.xy, instance.uv.zw, offset);
    out.color = instance.color;
    out.flags = instance.flags;
    return out;
}

fragment float4 markdown_glyph_fragment(
    GlyphOut in [[stage_in]],
    texture2d<float> atlas [[texture(0)]],
    sampler atlas_sampler [[sampler(0)]])
{
    float4 atlas_sample = atlas.sample(atlas_sampler, in.uv);
    float alpha = atlas_sample.a;
    if (alpha < 0.01)
        discard_fragment();
    bool color_glyph = (in.flags & STYLE_FLAG_COLOR_GLYPH) != 0u;
    return color_glyph ? atlas_sample : float4(in.color.rgb, in.color.a * alpha);
}
```

- [ ] **Step 2: Compile and stage the Metal library**

In `cmake/CompileShaders_Metal.cmake`, add `markdown.metal` compile/link commands that produce `${SHADER_OUTPUT_DIR}/markdown.metallib`, and add `compile_markdown_shaders`.

In root `CMakeLists.txt`, stage `${CMAKE_BINARY_DIR}/shaders/markdown.metallib` and add `compile_markdown_shaders` plus `stage_markdown_metal_shader` as `draxul` dependencies on Apple.

- [ ] **Step 3: Replace Metal bitmap state with buffers and atlas textures**

In `markdown_render_pass_metal.mm`, replace texture-only frame resources with:

```objc
struct AtlasTexture
{
    ObjCRef<id<MTLTexture>> texture;
    uint32_t generation = 0;
    uint64_t upload_revision = 0;
};

struct FrameResources
{
    ObjCRef<id<MTLBuffer>> rect_buffer;
    ObjCRef<id<MTLBuffer>> glyph_buffer;
    size_t rect_buffer_size = 0;
    size_t glyph_buffer_size = 0;
    uint64_t uploaded_draw_revision = 0;
    std::map<uint32_t, AtlasTexture> atlases;
};
```

Add `rect_pipeline` and `glyph_pipeline`.

- [ ] **Step 4: Implement Metal uploads and draw calls**

In `record_prepass()`:

1. Ensure buffers sized to `rects.size() * sizeof(MarkdownRectInstance)` and `glyphs.size() * sizeof(MarkdownGlyphInstance)`.
2. Copy instance vectors into shared buffers.
3. Ensure per-frame atlas textures and call `replaceRegion` for full or dirty uploads.

In `record()`:

1. Draw rects with `markdown_rect_vertex/fragment`.
2. Draw glyph batches with `markdown_glyph_vertex/fragment`, binding the batch atlas texture.

- [ ] **Step 5: Build on macOS**

Run on macOS:

```bash
cmake --preset mac-release
cmake --build build --target draxul draxul-tests
```

Expected: app and tests build.

---

## Task 7: Remove MarkdownBitmap Path

**Files:**
- Delete: `libs/draxul-markdown/include/draxul/markdown/markdown_bitmap.h`
- Delete: `libs/draxul-markdown/src/markdown_bitmap.cpp`
- Delete: `tests/markdown_bitmap_tests.cpp`
- Modify: `libs/draxul-markdown/CMakeLists.txt`
- Modify: all includes that still mention `markdown_bitmap.h`

- [ ] **Step 1: Delete bitmap files**

Remove:

```text
libs/draxul-markdown/include/draxul/markdown/markdown_bitmap.h
libs/draxul-markdown/src/markdown_bitmap.cpp
tests/markdown_bitmap_tests.cpp
```

- [ ] **Step 2: Remove bitmap source from CMake**

In `libs/draxul-markdown/CMakeLists.txt`, remove:

```cmake
src/markdown_bitmap.cpp
```

- [ ] **Step 3: Confirm no bitmap references remain**

Run:

```powershell
rg -n "MarkdownBitmap|markdown_bitmap|paint_markdown_bitmap" libs tests app
```

Expected: no matches.

- [ ] **Step 4: Build**

Run:

```powershell
cmake --build build --config Release --target draxul draxul-tests
```

Expected: build succeeds.

---

## Task 8: Add End-To-End Markdown Render Verification

**Files:**
- Modify: `tests/markdown_draw_list_tests.cpp`
- Modify: `docs/features.md`

- [ ] **Step 1: Add a sample stress markdown file for manual/runtime checks**

Use the existing `build/markdown-sample.md` if present. If it is not present, create it manually before manual testing with:

````markdown
---
title: GPU Markdown Smoke
tags: [draxul, markdown, renderer]
---

# Heading One

Paragraph text with **bold-looking style source** and enough words to wrap through several visual rows.

> Quote body with a background and accent bar.

- Bullet item
- [x] Checked task
- [ ] Unchecked task

```cpp
int main() {
    return 0;
}
```

---

https://example.com/this/is/a/very/long/unbroken/url/that/must/not/create/a/huge/atlas/cluster/or/crash/while/paging
````

- [ ] **Step 2: Add draw-list regression for long unbroken text**

Add this test to `tests/markdown_draw_list_tests.cpp`:

```cpp
TEST_CASE("markdown draw-list handles long unbroken text without huge glyph instances", "[markdown][drawlist]")
{
    auto service = make_initialized_service();
    auto theme = default_markdown_theme(12.0f);

    LayoutDocument layout;
    LayoutRow row;
    row.y = 0.0f;
    row.height = 40.0f;
    row.baseline = 28.0f;
    row.runs.push_back(TextRun{
        .text = std::string(512, 'W'),
        .style = StyleId{},
        .x = 0.0f,
        .baseline = 28.0f,
    });
    layout.rows.push_back(std::move(row));

    const auto list = build_markdown_draw_list(
        layout,
        theme,
        service,
        MarkdownDrawListOptions{ .viewport_width = 400, .viewport_height = 80 });

    REQUIRE_FALSE(list.glyphs.empty());
    for (const auto& glyph : list.glyphs)
    {
        REQUIRE(glyph.rect.z < 512.0f);
        REQUIRE(glyph.rect.w > 0.0f);
    }

    service.shutdown();
}
```

- [ ] **Step 3: Update docs/features.md**

Update the markdown viewer entry so it says the host renders via GPU draw-list glyph and decoration instances using Draxul rich-text atlases. Remove any note that says markdown is CPU-composited into an RGBA bitmap.

- [ ] **Step 4: Run focused tests and smoke**

Run:

```powershell
cmake --build build --config Release --target draxul draxul-tests
.\build\tests\Release\draxul-tests.exe "[markdown][drawlist],[markdown][layout],[markdown][scroll],[font][richtext]"
$output = & .\build\Release\draxul.exe --console --smoke-test --host markdown --source build\markdown-sample.md 2>&1; $output; if ($output -match 'Atlas full|Glyph atlas reset') { exit 1 }
```

Expected: tests pass and markdown smoke exits without atlas exhaustion warnings.

- [ ] **Step 5: Run full verification**

Run:

```powershell
ctest --test-dir build --build-config Release --output-on-failure
python do.py smoke
git diff --check
```

Expected: all tests pass. `git diff --check` may print CRLF normalization warnings, but must not report whitespace errors.

---

## Recommended Agent Split

- Agent 1: Task 1, `RichTextService` atlas id/snapshot API and tests.
- Agent 2: Tasks 2-3, draw-list data model, builder, decoration parity, atlas upload payloads, tests.
- Agent 3: Task 5, Vulkan render pass and shaders.
- Agent 4: Task 6, Metal render pass and shader/CMake wiring.
- Parent session: Task 4 integration, Task 7 deletion, Task 8 final verification, conflict resolution, and docs.

The parent should not let Agents 3 and 4 start editing render-pass headers until Agents 1 and 2 have landed the shared APIs, because both platform passes depend on the same final instance structs and upload payload shape.

## Self-Review

- Spec coverage: full current markdown viewer parity is covered by Tasks 3, 4, 5, 6, 7, and 8.
- Bitmap removal: Task 7 deletes the old files and verifies no references remain.
- Cross-platform coverage: Vulkan and Metal are separate tasks with explicit shader/CMake work.
- Atlas reset safety: Task 1 exposes generations and reset state; Task 3 copies upload payloads after draw-list building.
- Test coverage: unit tests cover struct layout, decoration conversion, visible-row clipping, atlas grouping, and long unbroken text; smoke tests cover app startup.

#pragma once

#include "text_service.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace draxul
{

using RichTextAtlasId = uint32_t;

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
    RichTextAtlasId atlas_id = 0;
    uint32_t atlas_generation = 0;
    float advance_px = 0.0f;
};

struct RichTextAtlasSnapshot
{
    RichTextAtlasId atlas_id = 0;
    uint32_t generation = 0;
    const uint8_t* data = nullptr;
    int width = 0;
    int height = 0;
    AtlasDirtyRect dirty_rect{};
    bool dirty = false;
    bool reset_pending = false;
};

class RichTextService : public IGlyphAtlas
{
public:
    RichTextService();
    ~RichTextService() override;
    RichTextService(const RichTextService&) = delete;
    RichTextService& operator=(const RichTextService&) = delete;
    RichTextService(RichTextService&& other) noexcept;
    RichTextService& operator=(RichTextService&& other) noexcept;

    bool initialize(const TextServiceConfig& config, float base_point_size, float display_ppi);
    void shutdown();

    RichTextCluster resolve_cluster(const std::string& text, const RichTextStyleKey& style);
    const FontMetrics& metrics_for(const RichTextStyleKey& style);
    std::vector<RichTextAtlasSnapshot> atlas_snapshots() const;
    std::optional<RichTextAtlasSnapshot> atlas_snapshot(RichTextAtlasId atlas_id) const;
    bool consume_any_atlas_reset();
    void clear_atlas_dirty(RichTextAtlasId atlas_id);
    void clear_all_atlas_dirty();

    // Rich text styles are backed by separate TextService instances, so each
    // style owns its own atlas. The IGlyphAtlas compatibility path remains
    // bound to the base style's single backing TextService.
    AtlasRegion resolve_cluster(const std::string& text, bool is_bold, bool is_italic) override;
    int ligature_cell_span(const std::string& text, bool is_bold, bool is_italic) override;

    bool atlas_dirty() const override;
    bool consume_atlas_reset() override;
    void clear_atlas_dirty() override;
    const uint8_t* atlas_data() const override;
    int atlas_width() const override;
    int atlas_height() const override;
    AtlasDirtyRect atlas_dirty_rect() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace draxul

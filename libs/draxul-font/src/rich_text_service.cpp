#include <draxul/rich_text_service.h>

#include <algorithm>
#include <map>

namespace draxul
{

namespace
{

RichTextStyleKey normalize_style(RichTextStyleKey style)
{
    style.point_size = std::clamp(style.point_size, TextService::MIN_POINT_SIZE, TextService::MAX_POINT_SIZE);
    return style;
}

struct RichTextStyleKeyLess
{
    bool operator()(const RichTextStyleKey& lhs, const RichTextStyleKey& rhs) const
    {
        if (lhs.point_size != rhs.point_size)
            return lhs.point_size < rhs.point_size;
        if (lhs.bold != rhs.bold)
            return lhs.bold < rhs.bold;
        return lhs.italic < rhs.italic;
    }
};

float cluster_advance_px(const AtlasRegion& atlas, const FontMetrics& metrics)
{
    if (atlas.advance_px > 0)
        return static_cast<float>(atlas.advance_px);
    return static_cast<float>(std::max(atlas.bitmap_size.x, metrics.cell_width));
}

} // namespace

struct RichTextService::Impl
{
    struct StyleService
    {
        RichTextAtlasId atlas_id = 0;
        uint32_t generation = 1;
        bool reset_pending = false;
        bool reset_available = false;
        std::unique_ptr<TextService> service;
    };

    TextServiceConfig config;
    float display_ppi = 96.0f;
    RichTextStyleKey base_style;
    RichTextStyleKey active_style;
    bool initialized = false;
    RichTextAtlasId next_atlas_id = 1;

    std::map<RichTextStyleKey, StyleService, RichTextStyleKeyLess> services;

    StyleService* service_for(const RichTextStyleKey& requested_style, bool make_active)
    {
        if (!initialized)
            return nullptr;

        const auto style = normalize_style(requested_style);
        auto it = services.find(style);
        if (it != services.end())
        {
            if (make_active)
                active_style = style;
            return &it->second;
        }

        auto service = std::make_unique<TextService>();
        if (!service->initialize(config, style.point_size, display_ppi))
            return nullptr;

        StyleService style_service;
        style_service.atlas_id = next_atlas_id++;
        style_service.service = std::move(service);

        auto [inserted, _] = services.emplace(style, std::move(style_service));
        if (make_active)
            active_style = style;
        return &inserted->second;
    }

    StyleService* active_service()
    {
        if (!initialized)
            return nullptr;

        auto it = services.find(active_style);
        if (it != services.end())
            return &it->second;

        it = services.find(base_style);
        return it != services.end() ? &it->second : nullptr;
    }

    const StyleService* active_service() const
    {
        if (!initialized)
            return nullptr;

        auto it = services.find(active_style);
        if (it != services.end())
            return &it->second;

        it = services.find(base_style);
        return it != services.end() ? &it->second : nullptr;
    }

    StyleService* base_service()
    {
        return service_for(base_style, false);
    }

    const StyleService* base_service() const
    {
        if (!initialized)
            return nullptr;

        auto it = services.find(base_style);
        return it != services.end() ? &it->second : nullptr;
    }

    StyleService* service_for_atlas(RichTextAtlasId atlas_id)
    {
        for (auto& entry : services)
        {
            if (entry.second.atlas_id == atlas_id)
                return &entry.second;
        }
        return nullptr;
    }

    const StyleService* service_for_atlas(RichTextAtlasId atlas_id) const
    {
        for (const auto& entry : services)
        {
            if (entry.second.atlas_id == atlas_id)
                return &entry.second;
        }
        return nullptr;
    }

    bool update_reset_state(StyleService& style_service)
    {
        if (style_service.service == nullptr || !style_service.service->consume_atlas_reset())
            return false;

        ++style_service.generation;
        style_service.reset_pending = true;
        style_service.reset_available = true;
        return true;
    }

    RichTextAtlasSnapshot snapshot_for(const StyleService& style_service) const
    {
        const auto* service = style_service.service.get();
        if (service == nullptr)
            return {};

        return { style_service.atlas_id,
                 style_service.generation,
                 service->atlas_data(),
                 service->atlas_width(),
                 service->atlas_height(),
                 service->atlas_dirty_rect(),
                 service->atlas_dirty(),
                 style_service.reset_pending };
    }
};

RichTextService::RichTextService()
    : impl_(std::make_unique<Impl>())
{
}

RichTextService::~RichTextService() = default;

RichTextService::RichTextService(RichTextService&& other) noexcept = default;

RichTextService& RichTextService::operator=(RichTextService&& other) noexcept = default;

bool RichTextService::initialize(const TextServiceConfig& config, float base_point_size, float display_ppi)
{
    shutdown();

    impl_->config = config;
    impl_->display_ppi = display_ppi;
    impl_->base_style = normalize_style({ base_point_size, false, false });
    impl_->active_style = impl_->base_style;
    impl_->initialized = true;

    if (impl_->service_for(impl_->base_style, true) != nullptr)
        return true;

    shutdown();
    return false;
}

void RichTextService::shutdown()
{
    for (auto& entry : impl_->services)
    {
        auto& style_service = entry.second;
        if (style_service.service != nullptr)
            style_service.service->shutdown();
    }
    impl_->services.clear();
    impl_->initialized = false;
    impl_->base_style = {};
    impl_->active_style = {};
    impl_->next_atlas_id = 1;
}

RichTextCluster RichTextService::resolve_cluster(const std::string& text, const RichTextStyleKey& style)
{
    auto normalized = normalize_style(style);
    auto* style_service = impl_->service_for(normalized, true);
    if (style_service == nullptr || style_service->service == nullptr)
        return {};

    RichTextCluster cluster;
    cluster.atlas = style_service->service->resolve_cluster(text, normalized.bold, normalized.italic);
    impl_->update_reset_state(*style_service);
    cluster.metrics = style_service->service->metrics();
    cluster.atlas_id = style_service->atlas_id;
    cluster.atlas_generation = style_service->generation;
    cluster.advance_px = cluster_advance_px(cluster.atlas, cluster.metrics);
    return cluster;
}

const FontMetrics& RichTextService::metrics_for(const RichTextStyleKey& style)
{
    auto* style_service = impl_->service_for(style, false);
    if (style_service == nullptr || style_service->service == nullptr)
    {
        static const FontMetrics empty_metrics{};
        return empty_metrics;
    }

    return style_service->service->metrics();
}

std::vector<RichTextAtlasSnapshot> RichTextService::atlas_snapshots() const
{
    std::vector<RichTextAtlasSnapshot> snapshots;
    snapshots.reserve(impl_->services.size());

    for (const auto& entry : impl_->services)
        snapshots.push_back(impl_->snapshot_for(entry.second));

    return snapshots;
}

std::optional<RichTextAtlasSnapshot> RichTextService::atlas_snapshot(RichTextAtlasId atlas_id) const
{
    const auto* style_service = impl_->service_for_atlas(atlas_id);
    if (style_service == nullptr)
        return std::nullopt;

    return impl_->snapshot_for(*style_service);
}

bool RichTextService::consume_any_atlas_reset()
{
    bool any_reset = false;
    for (auto& entry : impl_->services)
    {
        if (impl_->update_reset_state(entry.second))
            any_reset = true;
        if (entry.second.reset_available)
        {
            any_reset = true;
            entry.second.reset_available = false;
        }
    }
    return any_reset;
}

void RichTextService::clear_atlas_dirty(RichTextAtlasId atlas_id)
{
    auto* style_service = impl_->service_for_atlas(atlas_id);
    if (style_service == nullptr || style_service->service == nullptr)
        return;

    style_service->service->clear_atlas_dirty();
    style_service->reset_pending = false;
}

void RichTextService::clear_all_atlas_dirty()
{
    for (auto& entry : impl_->services)
    {
        auto& style_service = entry.second;
        if (style_service.service != nullptr)
            style_service.service->clear_atlas_dirty();
        style_service.reset_pending = false;
    }
}

AtlasRegion RichTextService::resolve_cluster(const std::string& text, bool is_bold, bool is_italic)
{
    auto* style_service = impl_->base_service();
    if (style_service == nullptr || style_service->service == nullptr)
        return {};

    const auto atlas = style_service->service->resolve_cluster(text, is_bold, is_italic);
    impl_->update_reset_state(*style_service);
    return atlas;
}

int RichTextService::ligature_cell_span(const std::string& text, bool is_bold, bool is_italic)
{
    auto* style_service = impl_->base_service();
    return style_service != nullptr && style_service->service != nullptr
               ? style_service->service->ligature_cell_span(text, is_bold, is_italic)
               : 0;
}

bool RichTextService::atlas_dirty() const
{
    const auto* style_service = impl_->base_service();
    return style_service != nullptr && style_service->service != nullptr && style_service->service->atlas_dirty();
}

bool RichTextService::consume_atlas_reset()
{
    auto* style_service = impl_->base_service();
    if (style_service == nullptr)
        return false;

    impl_->update_reset_state(*style_service);
    if (!style_service->reset_available)
        return false;

    style_service->reset_available = false;
    return true;
}

void RichTextService::clear_atlas_dirty()
{
    auto* style_service = impl_->base_service();
    if (style_service != nullptr)
        clear_atlas_dirty(style_service->atlas_id);
}

const uint8_t* RichTextService::atlas_data() const
{
    const auto* style_service = impl_->base_service();
    return style_service != nullptr && style_service->service != nullptr ? style_service->service->atlas_data() : nullptr;
}

int RichTextService::atlas_width() const
{
    const auto* style_service = impl_->base_service();
    return style_service != nullptr && style_service->service != nullptr ? style_service->service->atlas_width() : 0;
}

int RichTextService::atlas_height() const
{
    const auto* style_service = impl_->base_service();
    return style_service != nullptr && style_service->service != nullptr ? style_service->service->atlas_height() : 0;
}

AtlasDirtyRect RichTextService::atlas_dirty_rect() const
{
    const auto* style_service = impl_->base_service();
    return style_service != nullptr && style_service->service != nullptr ? style_service->service->atlas_dirty_rect()
                                                                        : AtlasDirtyRect{};
}

} // namespace draxul

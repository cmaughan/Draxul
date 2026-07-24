#pragma once

#include "chrome_layout.h"

#include <draxul/renderer.h>
#include <draxul/text_service.h>
#include <memory>
#include <string_view>
#include <unordered_map>

namespace draxul
{

// Maintains the grid-backed text handles layered over ChromeVectorPass.
class ChromeTextLayer
{
public:
    ChromeTextLayer(IGridRenderer* renderer, TextService* text_service);
    void shutdown();
    void draw(IFrameContext& frame, const ChromeLayoutOutput& layout, const ChromeTheme& theme);

private:
    void update_top_bar(const ChromeLayoutOutput& layout);
    void update_sidebar(const ChromeLayoutOutput& layout, const ChromeTheme& theme);
    void update_section_header(std::unique_ptr<IGridHandle>& handle,
        const ChromeLayoutOutput& layout, const ChromeRect& rect,
        std::string_view label, const Color& color);
    void update_panes(const ChromeLayoutOutput& layout);

    IGridRenderer* renderer_ = nullptr;
    TextService* text_service_ = nullptr;
    std::unique_ptr<IGridHandle> top_bar_handle_;
    std::unique_ptr<IGridHandle> sidebar_handle_;
    std::unique_ptr<IGridHandle> agents_handle_;
    std::unique_ptr<IGridHandle> spaces_header_handle_;
    std::unique_ptr<IGridHandle> agents_header_handle_;
    std::unordered_map<LeafId, std::unique_ptr<IGridHandle>> pane_handles_;
};

} // namespace draxul

#pragma once

#include "chrome_layout.h"

#include <draxul/nanovg_pass.h>
#include <draxul/renderer.h>
#include <memory>

namespace draxul
{

// Owns the renderer-neutral NanoVG pass and translates pure Chrome geometry
// into vector commands. ChromeHost only schedules the pass.
class ChromeVectorPass
{
public:
    bool initialize();
    void shutdown();
    [[nodiscard]] bool available() const;
    void record(IFrameContext& frame, const ChromeLayoutOutput& layout,
        const ChromeTheme& theme, int viewport_width, int viewport_height);

private:
    std::unique_ptr<INanoVGPass> pass_;
};

} // namespace draxul

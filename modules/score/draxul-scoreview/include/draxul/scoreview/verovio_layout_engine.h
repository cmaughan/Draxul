#pragma once

#include <draxul/scoreview/layout_engine.h>

#include <memory>

namespace draxul
{
namespace scoreview
{

// Verovio-backed engraving engine. Verovio types stay private to the .cpp
// (pimpl) so the dependency never leaks into consumers. The resource
// directory is Verovio's data/ tree (SMuFL fonts + glyph metadata), staged at
// build time as <exe-dir>/verovio-data; tests point at the fetched source
// tree directly.
class VerovioLayoutEngine final : public ILayoutEngine
{
public:
    // Returns nullptr and fills `error` when the resource directory is
    // missing or unloadable.
    static std::unique_ptr<VerovioLayoutEngine> create(
        const std::string& resource_dir, std::string& error);
    ~VerovioLayoutEngine() override;

    bool load(std::string_view bytes, std::string& error) override;
    void set_options(const LayoutOptions& options) override;
    bool is_loaded() const override;
    int page_count() override;
    std::string render_page_svg(int page_number) override;

private:
    VerovioLayoutEngine();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace scoreview
} // namespace draxul

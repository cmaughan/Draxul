#include <draxul/scoreview/verovio_layout_engine.h>

#include <draxul/log.h>

#include <vrv/toolkit.h>

#include <nlohmann/json.hpp>
#include <tinyxml2.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>

namespace draxul
{
namespace scoreview
{

namespace
{

// Verovio page dimensions are abstract units where the rendered SVG pixel
// size is dimension * scale / 100. We hold a fixed base engraving scale and
// derive the page dimensions from the requested pixel size, folding the
// display's pixel_scale into the scale so music keeps its physical size on
// hidpi displays.
constexpr int BASE_ENGRAVE_SCALE_PERCENT = 40;

int effective_scale_percent(float pixel_scale)
{
    const float scale = BASE_ENGRAVE_SCALE_PERCENT * std::max(pixel_scale, 0.25f);
    return std::clamp(static_cast<int>(std::lround(scale)), 10, 400);
}

int to_verovio_dimension(int px, int scale_percent)
{
    const long long dimension = std::llround(px * 100.0 / scale_percent);
    return static_cast<int>(std::clamp<long long>(dimension, 100, 60000));
}

std::string options_json(const LayoutOptions& options)
{
    // Verovio's SetOptions merges keys into the current option set, so both
    // branches emit the full symmetric set — toggling Flow → Paged must undo
    // breaks/adjust/header explicitly.
    std::string json = "{";
    if (options.mode == LayoutMode::Flow)
    {
        // One endless system. Page dimensions are irrelevant (adjustPage*
        // resizes the single page to its content); the emitted canvas is
        // resolution-independent and the host picks the on-screen strip
        // height, so a fixed engraving scale is fine.
        json += "\"breaks\": \"none\"";
        json += ", \"adjustPageWidth\": true";
        json += ", \"adjustPageHeight\": true";
        json += ", \"header\": \"none\"";
        json += ", \"scale\": " + std::to_string(BASE_ENGRAVE_SCALE_PERCENT);
        json += ", \"pageWidth\": 2100, \"pageHeight\": 2970";
    }
    else
    {
        const int scale = effective_scale_percent(options.pixel_scale);
        json += "\"breaks\": \"auto\"";
        json += ", \"adjustPageWidth\": false";
        json += ", \"adjustPageHeight\": false";
        json += ", \"header\": \"auto\"";
        json += ", \"pageWidth\": " + std::to_string(to_verovio_dimension(options.page_size_px.x, scale));
        json += ", \"pageHeight\": " + std::to_string(to_verovio_dimension(options.page_size_px.y, scale));
        json += ", \"scale\": " + std::to_string(scale);
    }
    json += ", \"footer\": \"none\"";
    json += ", \"svgViewBox\": true";
    json += "}";
    return json;
}

bool looks_like_zip(std::string_view bytes)
{
    return bytes.size() >= 4 && bytes.compare(0, 4, "PK\x03\x04") == 0;
}

} // namespace

struct VerovioLayoutEngine::Impl
{
    vrv::Toolkit toolkit{ /*initFont=*/false };
    LayoutOptions options;
    bool loaded = false;
};

VerovioLayoutEngine::VerovioLayoutEngine()
    : impl_(std::make_unique<Impl>())
{
}

VerovioLayoutEngine::~VerovioLayoutEngine() = default;

std::unique_ptr<VerovioLayoutEngine> VerovioLayoutEngine::create(
    const std::string& resource_dir, std::string& error)
{
    // make_unique can't reach the private constructor.
    std::unique_ptr<VerovioLayoutEngine> engine(new VerovioLayoutEngine());
    if (!engine->impl_->toolkit.SetResourcePath(resource_dir))
    {
        error = "Verovio resources not found or unloadable at '" + resource_dir + "'";
        return nullptr;
    }
    engine->impl_->toolkit.SetOptions(options_json(engine->impl_->options));
    DRAXUL_LOG_DEBUG(LogCategory::App, "verovio %s ready (resources: %s)",
        engine->impl_->toolkit.GetVersion().c_str(), resource_dir.c_str());
    return engine;
}

bool VerovioLayoutEngine::load(std::string_view bytes, std::string& error)
{
    if (bytes.empty())
    {
        error = "empty score data";
        return false;
    }

    const auto start = std::chrono::steady_clock::now();
    impl_->toolkit.SetOptions(options_json(impl_->options));

    bool ok = false;
    if (looks_like_zip(bytes))
        ok = impl_->toolkit.LoadZipDataBuffer(
            reinterpret_cast<const unsigned char*>(bytes.data()), static_cast<int>(bytes.size()));
    else
        ok = impl_->toolkit.LoadData(std::string(bytes));

    if (!ok)
    {
        impl_->loaded = false;
        error = "Verovio could not parse the score data";
        return false;
    }
    impl_->loaded = true;

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    DRAXUL_LOG_DEBUG(LogCategory::App, "verovio: load + layout took %lld ms (%d pages)",
        static_cast<long long>(elapsed.count()), impl_->toolkit.GetPageCount());
    return true;
}

void VerovioLayoutEngine::set_options(const LayoutOptions& options)
{
    impl_->options = options;
    impl_->toolkit.SetOptions(options_json(options));
    if (impl_->loaded)
    {
        const auto start = std::chrono::steady_clock::now();
        impl_->toolkit.RedoLayout();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        DRAXUL_LOG_DEBUG(LogCategory::App, "verovio: re-layout took %lld ms (%d pages)",
            static_cast<long long>(elapsed.count()), impl_->toolkit.GetPageCount());
    }
}

bool VerovioLayoutEngine::is_loaded() const
{
    return impl_->loaded;
}

int VerovioLayoutEngine::page_count()
{
    return impl_->loaded ? impl_->toolkit.GetPageCount() : 0;
}

std::string VerovioLayoutEngine::render_page_svg(int page_number)
{
    if (!impl_->loaded || page_number < 1 || page_number > page_count())
        return {};
    return impl_->toolkit.RenderToSVG(page_number, /*xmlDeclaration=*/false);
}

std::string VerovioLayoutEngine::render_timemap()
{
    if (!impl_->loaded)
        return {};
    return impl_->toolkit.RenderToTimemap();
}

int VerovioLayoutEngine::midi_pitch_for_element(const std::string& element_id)
{
    if (!impl_->loaded)
        return -1;
    const std::string json = impl_->toolkit.GetMIDIValuesForElement(element_id);
    const nlohmann::json doc = nlohmann::json::parse(json, /*cb=*/nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object())
        return -1;
    const auto pitch = doc.find("pitch");
    if (pitch == doc.end() || !pitch->is_number())
        return -1;
    const int value = pitch->get<int>();
    return (value > 0 && value < 128) ? value : -1;
}

namespace
{
void collect_tie_end_ids(const tinyxml2::XMLElement* element, std::vector<std::string>& out)
{
    for (const tinyxml2::XMLElement* child = element->FirstChildElement(); child != nullptr;
        child = child->NextSiblingElement())
    {
        if (std::strcmp(child->Name(), "tie") == 0)
        {
            const char* endid = child->Attribute("endid");
            if (endid != nullptr && endid[0] == '#')
                out.emplace_back(endid + 1);
        }
        collect_tie_end_ids(child, out);
    }
}
} // namespace

std::vector<std::string> VerovioLayoutEngine::tie_end_ids()
{
    std::vector<std::string> ids;
    if (!impl_->loaded)
        return ids;
    const std::string mei = impl_->toolkit.GetMEI("");
    tinyxml2::XMLDocument doc;
    if (doc.Parse(mei.data(), mei.size()) != tinyxml2::XML_SUCCESS || doc.RootElement() == nullptr)
    {
        DRAXUL_LOG_ERROR(LogCategory::App, "verovio: MEI export unparseable; ties unavailable");
        return ids;
    }
    collect_tie_end_ids(doc.RootElement(), ids);
    return ids;
}

} // namespace scoreview
} // namespace draxul

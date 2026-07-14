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
#include <unordered_map>

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
    // Lazily-parsed note id -> diatonic letter (0=C..6=B) from the MEI, so
    // the palette can color enharmonics apart. Cleared whenever the loaded
    // music or its layout changes; rebuilt on the next query.
    std::unordered_map<std::string, int> note_letters;
    bool note_letters_built = false;
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
    impl_->note_letters_built = false;

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
    impl_->note_letters_built = false;
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
int letter_from_pname(const char* pname)
{
    if (pname == nullptr || pname[0] == '\0')
        return -1;
    switch (pname[0])
    {
    case 'c':
    case 'C':
        return 0;
    case 'd':
    case 'D':
        return 1;
    case 'e':
    case 'E':
        return 2;
    case 'f':
    case 'F':
        return 3;
    case 'g':
    case 'G':
        return 4;
    case 'a':
    case 'A':
        return 5;
    case 'b':
    case 'B':
        return 6;
    default:
        return -1;
    }
}

void collect_note_letters(
    const tinyxml2::XMLElement* element, std::unordered_map<std::string, int>& out)
{
    for (const tinyxml2::XMLElement* child = element->FirstChildElement(); child != nullptr;
        child = child->NextSiblingElement())
    {
        if (std::strcmp(child->Name(), "note") == 0)
        {
            const char* id = child->Attribute("xml:id");
            const int letter = letter_from_pname(child->Attribute("pname"));
            if (id != nullptr && letter >= 0)
                out.emplace(id, letter);
        }
        collect_note_letters(child, out);
    }
}
} // namespace

int VerovioLayoutEngine::note_letter_for_element(const std::string& element_id)
{
    if (!impl_->loaded)
        return -1;
    if (!impl_->note_letters_built)
    {
        impl_->note_letters.clear();
        const std::string mei = impl_->toolkit.GetMEI("");
        tinyxml2::XMLDocument doc;
        if (doc.Parse(mei.data(), mei.size()) == tinyxml2::XML_SUCCESS
            && doc.RootElement() != nullptr)
            collect_note_letters(doc.RootElement(), impl_->note_letters);
        else
            DRAXUL_LOG_ERROR(
                LogCategory::App, "verovio: MEI export unparseable; note spellings unavailable");
        impl_->note_letters_built = true;
    }
    const auto found = impl_->note_letters.find(element_id);
    return found == impl_->note_letters.end() ? -1 : found->second;
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

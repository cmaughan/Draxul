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
#include <vector>

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
        // The proportional-spacing experiment: 1.0 spaces notes linearly in
        // duration (Verovio's default 0.6 gives short notes proportionally
        // more room — authentic engraving, uneven scroll speed).
        json += ", \"spacingNonLinear\": ";
        json += options.proportional_spacing ? "1.0" : "0.6";
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
        // The reading view always keeps authentic engraving spacing.
        json += ", \"spacingNonLinear\": 0.6";
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
    // Lazily-parsed MEI index harvested from a single GetMEI/parse: note id ->
    // diatonic letter (0=C..6=B) so the palette can color enharmonics apart,
    // plus the set of tie end-ids so tied continuations auto-satisfy their
    // gate. Cleared whenever the loaded music or its layout changes; both are
    // rebuilt together on the next query.
    std::unordered_map<std::string, int> note_letters;
    std::vector<std::string> tie_ends;
    bool mei_index_built = false;
    // Lazy note id -> sounding MIDI pitch cache (-1 = none). GetMIDIValuesForElement
    // is a per-element JSON round-trip and the analysis, gate and waterfall
    // passes each query the same ids, so the first lookup per id is memoized.
    std::unordered_map<std::string, int> note_midi;
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
    impl_->mei_index_built = false;
    impl_->note_midi.clear();

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
    impl_->mei_index_built = false;
    impl_->note_midi.clear();
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
    const auto cached = impl_->note_midi.find(element_id);
    if (cached != impl_->note_midi.end())
        return cached->second;

    int value = -1;
    const std::string json = impl_->toolkit.GetMIDIValuesForElement(element_id);
    const nlohmann::json doc = nlohmann::json::parse(json, /*cb=*/nullptr, /*allow_exceptions=*/false);
    if (!doc.is_discarded() && doc.is_object())
    {
        const auto pitch = doc.find("pitch");
        if (pitch != doc.end() && pitch->is_number())
        {
            const int p = pitch->get<int>();
            if (p > 0 && p < 128)
                value = p;
        }
    }
    // Cache misses (value == -1) too: an unresolvable id never resolves under
    // the same engraving, so there is no point re-querying it each pass.
    impl_->note_midi.emplace(element_id, value);
    return value;
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

// One recursive walk collects both facets we need from the MEI: note id ->
// diatonic letter, and the tie end-ids. A <note> is never a <tie>, so the
// element type selects at most one branch; the recursion always descends.
void collect_mei_index(const tinyxml2::XMLElement* element,
    std::unordered_map<std::string, int>& letters, std::vector<std::string>& tie_ends)
{
    for (const tinyxml2::XMLElement* child = element->FirstChildElement(); child != nullptr;
        child = child->NextSiblingElement())
    {
        const char* name = child->Name();
        if (std::strcmp(name, "note") == 0)
        {
            const char* id = child->Attribute("xml:id");
            const int letter = letter_from_pname(child->Attribute("pname"));
            if (id != nullptr && letter >= 0)
                letters.emplace(id, letter);
        }
        else if (std::strcmp(name, "tie") == 0)
        {
            const char* endid = child->Attribute("endid");
            if (endid != nullptr && endid[0] == '#')
                tie_ends.emplace_back(endid + 1);
        }
        collect_mei_index(child, letters, tie_ends);
    }
}

} // namespace

// Populates the note-letter map and tie-end list from a single MEI export.
// Verovio's GetMEI serializes the whole document and tinyxml2 re-parses it, so
// the two consumers (spelling + ties) share one round-trip per engraving.
void VerovioLayoutEngine::ensure_mei_index()
{
    if (impl_->mei_index_built)
        return;
    impl_->note_letters.clear();
    impl_->tie_ends.clear();
    const std::string mei = impl_->toolkit.GetMEI("");
    tinyxml2::XMLDocument doc;
    if (doc.Parse(mei.data(), mei.size()) == tinyxml2::XML_SUCCESS && doc.RootElement() != nullptr)
        collect_mei_index(doc.RootElement(), impl_->note_letters, impl_->tie_ends);
    else
        DRAXUL_LOG_ERROR(LogCategory::App,
            "verovio: MEI export unparseable; note spellings and ties unavailable");
    impl_->mei_index_built = true;
}

int VerovioLayoutEngine::note_letter_for_element(const std::string& element_id)
{
    if (!impl_->loaded)
        return -1;
    ensure_mei_index();
    const auto found = impl_->note_letters.find(element_id);
    return found == impl_->note_letters.end() ? -1 : found->second;
}

std::vector<std::string> VerovioLayoutEngine::tie_end_ids()
{
    if (!impl_->loaded)
        return {};
    ensure_mei_index();
    return impl_->tie_ends;
}

} // namespace scoreview
} // namespace draxul

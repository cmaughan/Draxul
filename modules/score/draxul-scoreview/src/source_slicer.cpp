#include <draxul/scoreview/source_slicer.h>

#include <tinyxml2.h>

#include <algorithm>
#include <map>
#include <vector>

namespace draxul
{
namespace scoreview
{

using namespace tinyxml2;

namespace
{

// The attribute state that must be re-declared at a window head, in the
// child order MusicXML expects inside <attributes>.
struct AttributeState
{
    std::string divisions; // element XML, e.g. <divisions>6</divisions>
    std::string key;
    std::string time;
    std::string staves;
    std::map<int, std::string> clefs; // by clef number (0 = unnumbered)

    bool has_any() const
    {
        return !divisions.empty() || !key.empty() || !time.empty() || !staves.empty()
            || !clefs.empty();
    }
};

std::string element_xml(const XMLElement* element)
{
    XMLPrinter printer;
    element->Accept(&printer);
    return printer.CStr();
}

} // namespace

struct SourceSlicer::Impl
{
    XMLDocument document;
    // Everything under <score-partwise> that is not a <part> (part-list,
    // work, identification, defaults...), in order, as XML text.
    std::vector<std::string> header_children;
    struct Part
    {
        std::string id;
        std::vector<const XMLElement*> measures;
        // State accumulated BEFORE each measure (what a window starting
        // here must re-declare).
        std::vector<AttributeState> state_before;
    };
    std::vector<Part> parts;
    std::vector<double> bar_start_q; // size = bar_count + 1
};

SourceSlicer::SourceSlicer()
    : impl_(std::make_unique<Impl>())
{
}

SourceSlicer::~SourceSlicer() = default;

bool SourceSlicer::ready() const
{
    return !impl_->parts.empty() && !impl_->parts.front().measures.empty();
}

int SourceSlicer::bar_count() const
{
    return ready() ? static_cast<int>(impl_->parts.front().measures.size()) : 0;
}

double SourceSlicer::bar_start_q(int bar) const
{
    if (impl_->bar_start_q.empty())
        return 0.0;
    const int clamped = std::clamp(bar, 0, static_cast<int>(impl_->bar_start_q.size()) - 1);
    return impl_->bar_start_q[static_cast<size_t>(clamped)];
}

double SourceSlicer::bar_quarters(int bar) const
{
    return bar_start_q(bar + 1) - bar_start_q(bar);
}

int SourceSlicer::bar_at(double stream_q) const
{
    const int bars = bar_count();
    for (int bar = 0; bar < bars; ++bar)
    {
        if (stream_q < impl_->bar_start_q[static_cast<size_t>(bar) + 1])
            return bar;
    }
    return bars > 0 ? bars - 1 : 0;
}

bool SourceSlicer::load(const std::string& musicxml, std::string& error)
{
    impl_ = std::make_unique<Impl>();
    if (impl_->document.Parse(musicxml.c_str(), musicxml.size()) != XML_SUCCESS)
    {
        error = "source did not parse as XML";
        return false;
    }
    const XMLElement* root = impl_->document.FirstChildElement("score-partwise");
    if (root == nullptr)
    {
        error = "source is not a score-partwise MusicXML document";
        return false;
    }

    for (const XMLElement* child = root->FirstChildElement(); child != nullptr;
        child = child->NextSiblingElement())
    {
        if (std::string(child->Name()) == "part")
        {
            Impl::Part part;
            part.id = child->Attribute("id") != nullptr ? child->Attribute("id") : "";
            AttributeState state;
            for (const XMLElement* measure = child->FirstChildElement("measure");
                measure != nullptr; measure = measure->NextSiblingElement("measure"))
            {
                part.state_before.push_back(state);
                part.measures.push_back(measure);
                // Accumulate the attribute state this measure declares.
                for (const XMLElement* attributes = measure->FirstChildElement("attributes");
                    attributes != nullptr;
                    attributes = attributes->NextSiblingElement("attributes"))
                {
                    if (const XMLElement* divisions = attributes->FirstChildElement("divisions"))
                        state.divisions = element_xml(divisions);
                    if (const XMLElement* key = attributes->FirstChildElement("key"))
                        state.key = element_xml(key);
                    if (const XMLElement* time = attributes->FirstChildElement("time"))
                        state.time = element_xml(time);
                    if (const XMLElement* staves = attributes->FirstChildElement("staves"))
                        state.staves = element_xml(staves);
                    for (const XMLElement* clef = attributes->FirstChildElement("clef");
                        clef != nullptr; clef = clef->NextSiblingElement("clef"))
                    {
                        const int number = clef->IntAttribute("number", 0);
                        state.clefs[number] = element_xml(clef);
                    }
                }
            }
            impl_->parts.push_back(std::move(part));
        }
        else
        {
            impl_->header_children.push_back(element_xml(child));
        }
    }
    if (!ready())
    {
        error = "source holds no measures";
        return false;
    }

    // Bar starts on the quarter axis from the notated time signatures.
    // (Implicit pickup bars would shift this — a recorded S2 limitation.)
    double q = 0.0;
    double bar_quarters = 4.0;
    const Impl::Part& first_part = impl_->parts.front();
    impl_->bar_start_q.push_back(0.0);
    for (size_t bar = 0; bar < first_part.measures.size(); ++bar)
    {
        const AttributeState& state = bar < first_part.state_before.size()
            ? first_part.state_before[bar]
            : AttributeState{};
        // The measure itself may declare a new time signature.
        std::string time_xml = state.time;
        for (const XMLElement* attributes = first_part.measures[bar]->FirstChildElement("attributes");
            attributes != nullptr; attributes = attributes->NextSiblingElement("attributes"))
        {
            if (const XMLElement* time = attributes->FirstChildElement("time"))
                time_xml = element_xml(time);
        }
        if (!time_xml.empty())
        {
            XMLDocument time_doc;
            if (time_doc.Parse(time_xml.c_str()) == XML_SUCCESS)
            {
                const XMLElement* time = time_doc.FirstChildElement("time");
                const XMLElement* beats = time != nullptr ? time->FirstChildElement("beats") : nullptr;
                const XMLElement* beat_type = time != nullptr ? time->FirstChildElement("beat-type") : nullptr;
                if (beats != nullptr && beat_type != nullptr)
                {
                    const double b = std::max(1.0, beats->DoubleText(4.0));
                    const double bt = std::max(1.0, beat_type->DoubleText(4.0));
                    bar_quarters = b * 4.0 / bt;
                }
            }
        }
        q += bar_quarters;
        impl_->bar_start_q.push_back(q);
    }
    return true;
}

std::string SourceSlicer::window_xml(int first_bar, int count) const
{
    if (!ready() || first_bar < 0 || count <= 0 || first_bar + count > bar_count())
        return {};
    std::vector<StreamBar> bars;
    bars.reserve(static_cast<size_t>(count));
    for (int bar = first_bar; bar < first_bar + count; ++bar)
        bars.push_back(StreamBar{ bar, {} });
    return window_xml_for(bars, first_bar);
}

int SourceSlicer::part_count() const
{
    return static_cast<int>(impl_->parts.size());
}

int SourceSlicer::divisions_at(int bar) const
{
    if (!ready())
        return 1;
    const Impl::Part& part = impl_->parts.front();
    const int last = static_cast<int>(part.state_before.size()) - 1;
    // state_before[b] is the state BEFORE bar b; the state in force AT
    // `bar` includes its own declarations, i.e. state_before[bar + 1].
    for (int at = std::min(bar + 1, last); at >= 0; --at)
    {
        const std::string& xml = part.state_before[static_cast<size_t>(at)].divisions;
        if (xml.empty())
            continue;
        XMLDocument doc;
        if (doc.Parse(xml.c_str()) == XML_SUCCESS)
        {
            if (const XMLElement* divisions = doc.FirstChildElement("divisions"))
                return std::max(1, divisions->IntText(1));
        }
    }
    return 1;
}

std::string SourceSlicer::window_xml_for(
    const std::vector<StreamBar>& bars, int context_bar) const
{
    if (!ready() || bars.empty())
        return {};
    bool has_fabricated = false;
    for (const StreamBar& bar : bars)
    {
        if (bar.source_bar >= bar_count())
            return {};
        if (bar.source_bar < 0)
        {
            if (bar.measure_xml.empty())
                return {};
            has_fabricated = true;
        }
    }
    // Fabricated measures are written for a single part (the grand staff);
    // multi-part sources stay verbatim-only.
    if (has_fabricated && impl_->parts.size() != 1)
        return {};
    context_bar = std::clamp(context_bar, 0, bar_count() - 1);

    std::string out;
    out += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<score-partwise version=\"4.0\">\n";
    for (const std::string& child : impl_->header_children)
        out += child + "\n";

    for (const Impl::Part& part : impl_->parts)
    {
        out += "<part id=\"" + part.id + "\">\n";
        for (size_t index = 0; index < bars.size(); ++index)
        {
            const StreamBar& item = bars[index];
            XMLDocument clone_doc;
            XMLElement* cloned = nullptr;
            if (item.source_bar >= 0)
            {
                const XMLElement* measure = part.measures[static_cast<size_t>(item.source_bar)];
                XMLNode* clone = measure->DeepClone(&clone_doc);
                clone_doc.InsertEndChild(clone);
                cloned = clone->ToElement();
            }
            else
            {
                if (clone_doc.Parse(item.measure_xml.c_str()) != XML_SUCCESS)
                    return {};
                cloned = clone_doc.FirstChildElement("measure");
                if (cloned == nullptr)
                    return {};
            }
            cloned->SetAttribute("number", std::to_string(index + 1).c_str());

            if (index == 0)
            {
                // Inject the accumulated state as a complete <attributes>
                // block BEFORE the measure's own content. A separate block
                // keeps MusicXML's child-order rules trivially satisfied;
                // anything the measure re-declares simply wins afterwards.
                const AttributeState& state = part.state_before[static_cast<size_t>(context_bar)];
                if (state.has_any())
                {
                    std::string inject = "<attributes>";
                    inject += state.divisions;
                    inject += state.key;
                    inject += state.time;
                    inject += state.staves;
                    for (const auto& [number, clef] : state.clefs)
                        inject += clef;
                    inject += "</attributes>";
                    XMLDocument inject_doc;
                    if (inject_doc.Parse(inject.c_str()) == XML_SUCCESS)
                    {
                        cloned->InsertFirstChild(
                            inject_doc.FirstChildElement("attributes")->DeepClone(&clone_doc));
                    }
                }
            }
            XMLPrinter printer;
            cloned->Accept(&printer);
            out += printer.CStr();
            out += "\n";
        }
        out += "</part>\n";
    }
    out += "</score-partwise>\n";
    return out;
}

} // namespace scoreview
} // namespace draxul

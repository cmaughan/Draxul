#include <draxul/scoreview/analysis_overlay.h>

#include <draxul/scoreview/keyboard_render_nvg.h>
#include <draxul/scoreview/score_render_nvg.h>

#include <nanovg.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <unordered_map>

namespace draxul
{
namespace scoreview
{

namespace
{

// The overlay's contract color: green = inferred, never engraved source.
constexpr NVGcolor overlay_green(float alpha = 0.92f)
{
    return NVGcolor{ { { 0.00f, 0.54f, 0.26f, alpha } } };
}

// Bar index for a qstamp: the last bar whose start is at or before q.
int bar_at(const std::vector<double>& starts, double q)
{
    if (starts.empty())
        return -1;
    const auto after = std::upper_bound(starts.begin(), starts.end(), q + 1e-9);
    return static_cast<int>(std::distance(starts.begin(), after)) - 1;
}

struct BoxRef
{
    int page = -1;
    int index = -1; // into pages[page].bars
};

} // namespace

AnalysisOverlay build_analysis_overlay(const std::vector<ScoreDrawList>& pages,
    const Timemap& timemap, const PieceProfile& profile,
    const std::vector<double>& bar_starts_q,
    const std::function<int(const std::string&)>& midi_pitch)
{
    AnalysisOverlay overlay;
    overlay.pages.resize(pages.size());
    if (pages.empty() || bar_starts_q.empty())
        return overlay;

    // Bar geometry, best source first:
    //
    // 1. The interpreter's per-measure boxes — every op inside the measure
    //    group contributes, so min.x/max.x ARE the barlines and the y band
    //    covers staff lines plus directions above/below. Encounter order is
    //    bar order for a single-part score, which is the overlay's audience
    //    (the composer itself is single-part-only).
    // 2. Fallback (no measures recorded): boxes from note glyphs mapped
    //    through the timemap — annotations then hug the notes rather than
    //    the bars, which is why this is only the fallback.
    bool have_measures = false;
    for (const ScoreDrawList& page : pages)
        have_measures |= !page.measures.empty();
    if (have_measures)
    {
        int bar = 0;
        for (size_t p = 0; p < pages.size(); ++p)
        {
            for (const MeasureBox& measure : pages[p].measures)
            {
                if (!measure.valid)
                {
                    ++bar; // an empty engraved measure still advances the count
                    continue;
                }
                AnalysisOverlay::BarBox box;
                box.bar = bar++;
                box.min = measure.min;
                box.max = measure.max;
                overlay.pages[p].bars.push_back(box);
            }
        }
    }
    else
    {
        // note element id -> source bar, straight off the analysis axis.
        std::unordered_map<std::string, int> id_bar;
        for (const TimemapEntry& entry : timemap.entries)
        {
            const int bar = bar_at(bar_starts_q, entry.qstamp);
            if (bar < 0)
                continue;
            for (const std::string& id : entry.note_on)
                id_bar.emplace(id, bar);
        }
        for (size_t p = 0; p < pages.size(); ++p)
        {
            std::map<int, AnalysisOverlay::BarBox> boxes;
            for (const GlyphInstance& glyph : pages[p].glyphs)
            {
                const auto found = id_bar.find(glyph.element_id);
                if (found == id_bar.end())
                    continue;
                const glm::vec2 at{ glyph.xform.e, glyph.xform.f };
                auto [it, fresh] = boxes.try_emplace(found->second);
                AnalysisOverlay::BarBox& box = it->second;
                if (fresh)
                {
                    box.bar = found->second;
                    box.min = box.max = at;
                }
                else
                {
                    box.min = glm::min(box.min, at);
                    box.max = glm::max(box.max, at);
                }
            }
            overlay.pages[p].bars.reserve(boxes.size());
            for (auto& [bar, box] : boxes)
                overlay.pages[p].bars.push_back(box);
        }
    }
    // Row indices via the x-reset rule (boxes are in bar order; systems run
    // left-to-right, so a bar starting left of its predecessor opens the
    // next row).
    for (AnalysisOverlay::Page& page : overlay.pages)
    {
        int row = 0;
        for (size_t i = 0; i < page.bars.size(); ++i)
        {
            if (i > 0 && page.bars[i].min.x < page.bars[i - 1].min.x - 1e-3f)
                ++row;
            page.bars[i].row = row;
        }
    }

    // Staff height proxy: median bar-box height. Note-point boxes hug the
    // staff, so this lands near the real staff extent — every overlay size
    // derives from it rather than from guessed canvas conventions.
    std::vector<float> heights;
    for (const AnalysisOverlay::Page& page : overlay.pages)
    {
        for (const AnalysisOverlay::BarBox& box : page.bars)
        {
            const float h = box.max.y - box.min.y;
            if (h > 0.0f)
                heights.push_back(h);
        }
    }
    if (!heights.empty())
    {
        std::nth_element(heights.begin(), heights.begin() + static_cast<long>(heights.size() / 2),
            heights.end());
        overlay.staff_height = heights[heights.size() / 2];
    }
    if (overlay.staff_height <= 0.0f)
        overlay.staff_height = pages.front().canvas_size.y * 0.03f;
    const float staff_h = overlay.staff_height;

    // Bar -> box lookup, and "first engraved bar at or after B" for bars
    // whose content is all rests (no note geometry of their own).
    std::map<int, BoxRef> by_bar;
    for (size_t p = 0; p < overlay.pages.size(); ++p)
    {
        for (size_t i = 0; i < overlay.pages[p].bars.size(); ++i)
            by_bar.try_emplace(overlay.pages[p].bars[i].bar,
                BoxRef{ static_cast<int>(p), static_cast<int>(i) });
    }
    const auto box_at_or_after = [&](int bar) -> BoxRef {
        const auto found = by_bar.lower_bound(bar);
        return found != by_bar.end() ? found->second : BoxRef{};
    };
    const auto box_of = [&](BoxRef ref) -> const AnalysisOverlay::BarBox& {
        return overlay.pages[static_cast<size_t>(ref.page)]
            .bars[static_cast<size_t>(ref.index)];
    };

    // Section openings get the heavier tick.
    std::vector<char> section_open(bar_starts_q.size(), 0);
    for (const PieceProfile::Section& section : profile.sections)
    {
        if (section.start_bar >= 0 && section.start_bar < static_cast<int>(section_open.size()))
            section_open[static_cast<size_t>(section.start_bar)] = 1;
    }

    // Page-header band: title/composer text lives OUTSIDE the measures, above
    // the first system. First-row annotation lanes must not climb into it.
    std::vector<float> header_bottom(pages.size(), 0.0f);
    for (size_t p = 0; p < pages.size(); ++p)
    {
        float first_row_top = pages[p].canvas_size.y;
        for (const AnalysisOverlay::BarBox& box : overlay.pages[p].bars)
        {
            if (box.row == 0)
                first_row_top = std::min(first_row_top, box.min.y);
        }
        for (const DrawText& text : pages[p].texts)
        {
            if (text.pos.y < first_row_top)
                header_bottom[p] = std::max(header_bottom[p], text.pos.y);
        }
    }

    // Full vertical extent of each system row (all bars on the row, not just
    // one phrase's) — span lines, labels and washes all anchor to it.
    std::vector<std::vector<glm::vec2>> row_extents(overlay.pages.size());
    for (size_t p = 0; p < overlay.pages.size(); ++p)
    {
        for (const AnalysisOverlay::BarBox& box : overlay.pages[p].bars)
        {
            auto& rows = row_extents[p];
            if (static_cast<size_t>(box.row) >= rows.size())
                rows.resize(static_cast<size_t>(box.row) + 1, { box.min.y, box.max.y });
            rows[static_cast<size_t>(box.row)].x
                = std::min(rows[static_cast<size_t>(box.row)].x, box.min.y);
            rows[static_cast<size_t>(box.row)].y
                = std::max(rows[static_cast<size_t>(box.row)].y, box.max.y);
        }
    }

    // Phrase spans: each phrase's bar boxes, one segment per system row (the
    // row index carries the grouping — no geometry heuristics).
    for (size_t pi = 0; pi < profile.phrases.size(); ++pi)
    {
        const PieceProfile::Phrase& phrase = profile.phrases[pi];
        bool first_segment = true;
        for (size_t p = 0; p < overlay.pages.size(); ++p)
        {
            const auto& bars = overlay.pages[p].bars;
            AnalysisOverlay::PhraseSpan span;
            int current_row = -1;
            bool open = false;
            const auto flush = [&]() {
                if (!open)
                    return;
                const glm::vec2 extent = row_extents[p][static_cast<size_t>(current_row)];
                // Lane 1 of the annotation stack, in the gap above the row.
                // Measure boxes span the full system band (dir text and all),
                // so the offset is a small fraction — and hard-clamped below
                // whatever sits above: the previous system, or the page
                // header on the first row. The lanes must live in the gap,
                // never inside a neighbouring system.
                const float above = current_row > 0
                    ? row_extents[p][static_cast<size_t>(current_row) - 1].y
                    : header_bottom[p];
                span.y = std::max(extent.x - 0.10f * staff_h, above + 0.04f * staff_h);
                span.row_y0 = extent.x;
                span.row_y1 = extent.y;
                overlay.pages[p].phrases.push_back(span);
                first_segment = false;
                open = false;
            };
            for (const AnalysisOverlay::BarBox& box : bars)
            {
                if (box.bar < phrase.start_bar || box.bar >= phrase.end_bar)
                    continue;
                if (!open || box.row != current_row)
                {
                    flush();
                    span = AnalysisOverlay::PhraseSpan{};
                    span.phrase_id = static_cast<int>(pi);
                    span.x0 = box.min.x;
                    span.confidence = static_cast<float>(phrase.confidence);
                    span.repeat_of = phrase.repeat_of;
                    span.transposed = phrase.transposed;
                    span.opens_phrase = first_segment;
                    span.opens_section = first_segment
                        && phrase.start_bar < static_cast<int>(section_open.size())
                        && section_open[static_cast<size_t>(phrase.start_bar)] != 0;
                    current_row = box.row;
                    open = true;
                }
                span.x1 = std::max(span.x1, box.max.x);
            }
            flush();
        }
    }

    // A bar box only spans its own notes — a bar of low notes has a top far
    // below the system's. Labels must clear the SYSTEM, so anchor them to
    // the full extent of the bar's row.
    const auto row_extent = [&](BoxRef ref) {
        const AnalysisOverlay::BarBox& box = box_of(ref);
        return row_extents[static_cast<size_t>(ref.page)][static_cast<size_t>(box.row)];
    };

    // Key labels: one per detected key region (the whole piece when the key
    // never moves), anchored above the region's first engraved bar's row.
    const auto add_key_label = [&](int bar, const PieceProfile::KeyEstimate& key) {
        const BoxRef ref = box_at_or_after(std::max(0, bar));
        if (ref.page < 0)
            return;
        const AnalysisOverlay::BarBox& box = box_of(ref);
        char text[96];
        std::snprintf(text, sizeof(text), "%s (%.2f)", key_name(key.tonic_pc, key.minor).c_str(),
            key.confidence);
        AnalysisOverlay::Label label;
        // Lane 2: above the span lane, with the same in-the-gap clamps (the
        // previous system's bottom, or the page header on the first row).
        const float above = box.row > 0
            ? row_extents[static_cast<size_t>(ref.page)][static_cast<size_t>(box.row) - 1].y
            : header_bottom[static_cast<size_t>(ref.page)];
        label.pos = { box.min.x,
            std::max(row_extent(ref).x - 0.20f * staff_h, above + 0.10f * staff_h) };
        label.text = text;
        label.emphasis = true;
        overlay.pages[static_cast<size_t>(ref.page)].labels.push_back(std::move(label));
    };
    if (profile.key_sections.empty())
    {
        add_key_label(0, profile.global_key);
    }
    else
    {
        for (const PieceProfile::KeySection& section : profile.key_sections)
            add_key_label(bar_at(bar_starts_q, section.start_q + 1e-6), section.key);
    }

    // Motif coloring: reconstruct the analysis's melody (top sounding pitch
    // per onset) from the timemap, then paint every note of every motif
    // occurrence in its motif's color — the spelling palette's seven
    // naturals, cycled by rank, same color for the same motif everywhere. A
    // note claimed by two motifs keeps the stronger (lower-rank) color.
    const size_t motif_count = midi_pitch ? profile.motifs.size() : 0;
    std::unordered_map<std::string, int> note_color; // element id -> palette
    if (motif_count > 0)
    {
        // THE melody, via the same sustain-aware skyline the miner used —
        // occurrence positions index that melody, so any other extraction
        // here would color the wrong notes.
        std::vector<std::vector<std::string>> onset_ids;
        const std::vector<AnalysisOnset> onsets
            = analysis_onsets_from_timemap(timemap, midi_pitch, &onset_ids);
        const std::vector<MelodyNote> skyline = skyline_melody(onsets);
        struct ColorNote
        {
            double q = 0.0;
            std::string id;
        };
        std::vector<ColorNote> melody;
        melody.reserve(skyline.size());
        for (const MelodyNote& note : skyline)
            melody.push_back({ note.qstamp, onset_ids[note.onset][note.voice] });
        std::vector<double> melody_q;
        melody_q.reserve(melody.size());
        for (const ColorNote& note : melody)
            melody_q.push_back(note.q);

        for (size_t m = 0; m < motif_count; ++m)
        {
            const PieceProfile::Motif& motif = profile.motifs[m];
            // Rank -> one of the seven natural (white-key) palette colors.
            const int palette = static_cast<int>(m % 7) * 3 + 1;
            const std::vector<double>& occurrences
                = motif.occurrences_q.empty() ? std::vector<double>{ motif.first_q }
                                              : motif.occurrences_q;
            for (const double q : occurrences)
            {
                const auto found = std::lower_bound(melody_q.begin(), melody_q.end(), q - 1e-6);
                if (found == melody_q.end() || std::abs(*found - q) > 1e-6)
                    continue;
                const size_t start = static_cast<size_t>(
                    std::distance(melody_q.begin(), found));
                // intervals.size() steps span intervals.size()+1 notes.
                for (size_t i = start;
                    i < melody.size() && i <= start + motif.intervals.size(); ++i)
                    note_color.try_emplace(melody[i].id, palette);
            }
            AnalysisOverlay::MotifLegend legend;
            legend.palette = palette;
            legend.count = motif.count;
            legend.shape = "[";
            for (size_t i = 0; i < motif.intervals.size(); ++i)
            {
                if (i > 0)
                    legend.shape += " ";
                legend.shape
                    += (motif.intervals[i] >= 0 ? "+" : "") + std::to_string(motif.intervals[i]);
            }
            legend.shape += "]";
            overlay.motif_legend.push_back(std::move(legend));
        }

        overlay.highlights.resize(pages.size());
        for (size_t p = 0; p < pages.size(); ++p)
        {
            overlay.highlights[p].build(pages[p]);
            for (const auto& [id, palette] : note_color)
                overlay.highlights[p].set_guidance(id, palette);
        }
    }

    size_t unique_phrases = 0;
    for (const PieceProfile::Phrase& phrase : profile.phrases)
        unique_phrases += phrase.repeat_of < 0 ? 1 : 0;
    char banner[256];
    std::snprintf(banner, sizeof(banner),
        "analysis · %s (%.2f) · %zu phrases (%zu unique) · %zu sections · structure %.2f · "
        "%zu chords · %zu motifs · %zu figures",
        key_name(profile.global_key.tonic_pc, profile.global_key.minor).c_str(),
        profile.global_key.confidence, profile.phrases.size(), unique_phrases,
        profile.sections.size(), profile.structure_confidence, profile.chords.size(),
        profile.motifs.size(), profile.figures.size());
    overlay.banner = banner;
    return overlay;
}

void draw_analysis_overlay(NVGcontext* vg, const AnalysisOverlay& overlay,
    const AnalysisOverlay::Page& page, glm::vec2 origin, float scale, float pixel_scale,
    const ScoreTextFonts& fonts, bool unique_active)
{
    if (overlay.staff_height <= 0.0f)
        return;
    // Geometry (anchors, spans) lives in canvas space and scales with the
    // engraving; text and stroke sizes are fixed pixels — annotations must
    // stay readable UI, not grow with the sheet.
    const float line_w = 2.0f * pixel_scale;
    const float tick_h = 14.0f * pixel_scale;
    const auto to_px = [&](glm::vec2 canvas) {
        return glm::vec2{ origin.x + canvas.x * scale, origin.y + canvas.y * scale };
    };

    // Labels share the narrow inter-system gaps, and key changes coincide
    // with phrase starts by construction — collisions are the norm, not the
    // exception. Every label measures itself and nudges right past anything
    // already placed.
    struct Placed
    {
        float x0, y0, x1, y1;
    };
    std::vector<Placed> placed;
    const auto place_text = [&](float x, float y, int align, const char* text) {
        float bounds[4] = { 0, 0, 0, 0 };
        nvgTextAlign(vg, align);
        nvgTextBounds(vg, x, y, text, nullptr, bounds);
        const float w = bounds[2] - bounds[0];
        for (int tries = 0; tries < 6; ++tries)
        {
            bool hit = false;
            for (const Placed& other : placed)
            {
                if (bounds[0] < other.x1 && bounds[2] > other.x0 && bounds[1] < other.y1
                    && bounds[3] > other.y0)
                {
                    const float shift = other.x1 + 4.0f * pixel_scale - bounds[0];
                    bounds[0] += shift;
                    bounds[2] += shift;
                    x += shift;
                    hit = true;
                    break;
                }
            }
            if (!hit)
                break;
        }
        placed.push_back({ bounds[0], bounds[1], bounds[2], bounds[3] });
        nvgText(vg, x, y, text, nullptr);
        (void)w;
    };

    for (const AnalysisOverlay::PhraseSpan& span : page.phrases)
    {
        const glm::vec2 a = to_px({ span.x0, span.y });
        const glm::vec2 b = to_px({ span.x1, span.y });
        nvgBeginPath(vg);
        nvgMoveTo(vg, a.x, a.y);
        nvgLineTo(vg, b.x, b.y);
        nvgStrokeColor(vg, overlay_green(0.55f));
        nvgStrokeWidth(vg, line_w);
        nvgStroke(vg);
        // The opening tick drops toward the staff from the barline; a
        // section start reads as ONE taller, heavier tick (a second parallel
        // mark just looked like an inconsistency).
        const float tick = span.opens_section ? tick_h * 1.6f : tick_h;
        nvgBeginPath(vg);
        nvgMoveTo(vg, a.x, a.y);
        nvgLineTo(vg, a.x, a.y + tick);
        nvgStrokeColor(vg, overlay_green());
        nvgStrokeWidth(vg,
            span.opens_section ? line_w * 2.0f : (span.opens_phrase ? line_w * 1.5f : line_w));
        nvgStroke(vg);
        // The wash centers its own "P11 = P1" — don't duplicate it here.
        const bool label_here
            = span.opens_phrase && !(unique_active && span.repeat_of >= 0);
        if (label_here && fonts.regular >= 0)
        {
            char text[64];
            if (span.repeat_of >= 0)
                std::snprintf(text, sizeof(text), "P%d %s P%d", span.phrase_id,
                    span.transposed ? "\u2248" : "=", span.repeat_of);
            else if (span.confidence > 0.0f)
                std::snprintf(text, sizeof(text), "P%d \u00b7%.2f", span.phrase_id, span.confidence);
            else
                std::snprintf(text, sizeof(text), "P%d", span.phrase_id);
            nvgFontFaceId(vg, fonts.regular);
            nvgFontSize(vg, 12.0f * pixel_scale);
            nvgFillColor(vg, overlay_green());
            place_text(a.x + 4.0f * pixel_scale, a.y - 2.0f * pixel_scale,
                NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, text);
        }
    }

    if (fonts.regular >= 0)
    {
        for (const AnalysisOverlay::Label& label : page.labels)
        {
            const glm::vec2 at = to_px(label.pos);
            nvgFontFaceId(vg, label.emphasis && fonts.bold >= 0 ? fonts.bold : fonts.regular);
            nvgFontSize(vg, (label.emphasis ? 15.0f : 12.0f) * pixel_scale);
            nvgFillColor(vg, overlay_green());
            // Emphasis (key) labels hang above their anchor, the rest below.
            place_text(at.x, at.y + (label.emphasis ? -4.0f : 4.0f) * pixel_scale,
                NVG_ALIGN_LEFT | (label.emphasis ? NVG_ALIGN_BOTTOM : NVG_ALIGN_TOP),
                label.text.c_str());
        }
    }
}

void draw_unique_wash(NVGcontext* vg, const AnalysisOverlay::Page& page, glm::vec2 origin,
    float scale, float pixel_scale, const ScoreTextFonts& fonts)
{
    // Ghost the restatements: a near-opaque paper wash over each repeated
    // phrase's row band, labelled with the original it repeats. What stays
    // at full strength IS the piece's unique material.
    for (const AnalysisOverlay::PhraseSpan& span : page.phrases)
    {
        if (span.repeat_of < 0)
            continue;
        // Generous vertically (covers stems/ledger lines), tight horizontally
        // (the neighbouring bar on the same row may be NEW material).
        const float pad_y = 6.0f * pixel_scale;
        const float pad_x = 2.0f * pixel_scale;
        const float x = origin.x + span.x0 * scale - pad_x;
        const float y = origin.y + span.row_y0 * scale - pad_y;
        const float w = (span.x1 - span.x0) * scale + 2.0f * pad_x;
        const float h = (span.row_y1 - span.row_y0) * scale + 2.0f * pad_y;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, w, h, 4.0f * pixel_scale);
        nvgFillColor(vg, nvgRGBAf(0.99f, 0.98f, 0.96f, 0.86f));
        nvgFill(vg);
        nvgStrokeColor(vg, overlay_green(0.35f));
        nvgStrokeWidth(vg, 1.0f * pixel_scale);
        nvgStroke(vg);
        if (fonts.regular >= 0 && span.opens_phrase)
        {
            char text[64];
            std::snprintf(text, sizeof(text), "P%d %s P%d", span.phrase_id,
                span.transposed ? "\u2248" : "=", span.repeat_of);
            nvgFontFaceId(vg, fonts.bold >= 0 ? fonts.bold : fonts.regular);
            nvgFontSize(vg, 14.0f * pixel_scale);
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgFillColor(vg, overlay_green());
            nvgText(vg, x + w * 0.5f, y + h * 0.5f, text, nullptr);
        }
    }
}

void draw_analysis_banner(NVGcontext* vg, const AnalysisOverlay& overlay, float pixel_scale,
    const ScoreTextFonts& fonts)
{
    if (overlay.banner.empty() || fonts.regular < 0)
        return;
    const float size = 13.0f * pixel_scale;
    const float legend_size = 11.0f * pixel_scale;
    const float pad = 6.0f * pixel_scale;
    nvgFontFaceId(vg, fonts.regular);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgFontSize(vg, size);
    float bounds[4] = { 0, 0, 0, 0 };
    nvgTextBounds(vg, pad, pad, overlay.banner.c_str(), nullptr, bounds);
    const float legend_y = bounds[3] + 3.0f * pixel_scale;
    const float swatch = 9.0f * pixel_scale;

    // Measure the legend row (swatch + "×6 [+2 ...]" per motif) so the
    // backdrop pill covers both lines.
    float legend_w = 0.0f;
    if (!overlay.motif_legend.empty())
    {
        nvgFontSize(vg, legend_size);
        for (const AnalysisOverlay::MotifLegend& motif : overlay.motif_legend)
        {
            const std::string text = "\u00d7" + std::to_string(motif.count) + " " + motif.shape;
            float lb[4] = { 0, 0, 0, 0 };
            nvgTextBounds(vg, 0, 0, text.c_str(), nullptr, lb);
            legend_w += swatch + 3.0f * pixel_scale + (lb[2] - lb[0]) + 10.0f * pixel_scale;
        }
        bounds[2] = std::max(bounds[2], pad + legend_w);
        bounds[3] = legend_y + swatch + 3.0f * pixel_scale;
    }

    nvgBeginPath(vg);
    nvgRoundedRect(vg, bounds[0] - pad * 0.6f, bounds[1] - pad * 0.4f,
        (bounds[2] - bounds[0]) + pad * 1.2f, (bounds[3] - bounds[1]) + pad * 0.8f,
        3.0f * pixel_scale);
    nvgFillColor(vg, nvgRGBAf(1.0f, 1.0f, 1.0f, 0.85f));
    nvgFill(vg);
    nvgFillColor(vg, overlay_green());
    nvgFontSize(vg, size);
    nvgText(vg, pad, pad, overlay.banner.c_str(), nullptr);

    // The legend: a colored swatch per motif (the note color on the sheet),
    // then how often it recurs and its interval shape.
    float x = pad;
    nvgFontSize(vg, legend_size);
    for (const AnalysisOverlay::MotifLegend& motif : overlay.motif_legend)
    {
        const unsigned char* rgb
            = kGuidancePalette[std::clamp(motif.palette, 0, kGuidancePaletteSize - 1)];
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, legend_y + 1.0f * pixel_scale, swatch, swatch, 2.0f * pixel_scale);
        nvgFillColor(vg, nvgRGBA(rgb[0], rgb[1], rgb[2], 255));
        nvgFill(vg);
        x += swatch + 3.0f * pixel_scale;
        const std::string text = "\u00d7" + std::to_string(motif.count) + " " + motif.shape;
        float lb[4] = { 0, 0, 0, 0 };
        nvgTextBounds(vg, x, legend_y, text.c_str(), nullptr, lb);
        nvgFillColor(vg, overlay_green());
        nvgText(vg, x, legend_y, text.c_str(), nullptr);
        x = lb[2] + 10.0f * pixel_scale;
    }
}

} // namespace scoreview
} // namespace draxul

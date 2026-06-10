#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace draxul
{

// Returns the codepoint if the cluster is a single codepoint that the glyph
// cache synthesizes procedurally — Box Drawing (U+2500–U+257F) and Block
// Elements (U+2580–U+259F) — otherwise 0. Font glyphs for these ranges leave
// anti-aliased seams because their ink does not exactly fill the pixel-rounded
// cell; drawing them at exact cell size guarantees seamless tiling.
uint32_t synthesized_box_codepoint(std::string_view utf8_cluster);

// Renders the glyph as an alpha coverage map of exactly w*h bytes
// (row-major, 0 = transparent, 255 = opaque). cp must be in the
// synthesized ranges; unknown codepoints produce an empty (all-zero) map.
std::vector<uint8_t> render_box_glyph(uint32_t cp, int w, int h);

} // namespace draxul

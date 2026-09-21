#pragma once

#include <cstddef>
#include <cstdint>
#include <draxul/types.h>
#include <string>
#include <string_view>
#include <vector>

namespace draxul
{

// UTF-8 decoding, Unicode width policy, and display-cluster segmentation are
// implemented in draxul-types. Keeping the tables out of this public header
// prevents every text consumer from compiling its own copy.
int utf8_sequence_length(uint8_t lead);
bool utf8_decode_next(std::string_view text, size_t& offset, uint32_t& cp);
uint32_t utf8_first_codepoint(std::string_view text);
std::vector<int> utf8_codepoint_indices(std::string_view text);

bool is_east_asian_wide(uint32_t cp);
bool is_east_asian_ambiguous(uint32_t cp);
bool is_default_emoji_presentation(uint32_t cp);
bool is_emoji_text_presentation_candidate(uint32_t cp);
bool is_width_ignorable(uint32_t cp);
bool is_regional_indicator(uint32_t cp);
bool is_emoji_modifier(uint32_t cp);
bool is_ascii_keycap_base(uint32_t cp);
int cluster_cell_width(std::string_view text, const UiOptions& options = {});

bool is_indic_virama(uint32_t cp);
bool utf8_codepoint_at_is_valid(
    std::string_view text, size_t offset, size_t& next_offset);
size_t utf8_validated_prefix_length(std::string_view text, size_t max_length);
size_t next_display_cluster_end(std::string_view text, size_t offset);

struct DisplayCluster
{
    std::string text;
    size_t byte_start = 0;
    size_t byte_end = 0;
    int cell_width = 1;
    bool replacement = false;
};

std::vector<DisplayCluster> display_clusters(
    std::string_view text, const UiOptions& options = {});
int display_cell_width(std::string_view text, const UiOptions& options = {});

} // namespace draxul

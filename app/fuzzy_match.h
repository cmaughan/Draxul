#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace draxul
{

struct FuzzyMatchResult
{
    int score = 0;
    std::vector<size_t> positions; // indices of matched characters in target
    bool matched = false;
};

// fzf-style fuzzy match. Case-insensitive.
// Returns matched=false when pattern chars cannot all be found in target.
[[nodiscard]] FuzzyMatchResult fuzzy_match(std::string_view pattern, std::string_view target);

} // namespace draxul

#include "fuzzy_match.h"

#include <algorithm>
#include <cctype>

namespace draxul
{

namespace
{

// fzf scoring constants
constexpr int kScoreMatch = 16;
constexpr int kScoreGapStart = -3;
constexpr int kScoreGapExtension = -1;
constexpr int kBonusBoundary = 8;
constexpr int kBonusCamel123 = 7;
constexpr int kBonusConsecutive = 4;
constexpr int kBonusFirstCharMultiplier = 2;

enum class CharClass
{
    Lower,
    Upper,
    Digit,
    Delimiter, // _ - . / : ; ,
    Other,
};

CharClass classify(char c)
{
    if (c >= 'a' && c <= 'z')
        return CharClass::Lower;
    if (c >= 'A' && c <= 'Z')
        return CharClass::Upper;
    if (c >= '0' && c <= '9')
        return CharClass::Digit;
    if (c == '_' || c == '-' || c == '.' || c == '/' || c == ':' || c == ';' || c == ',')
        return CharClass::Delimiter;
    return CharClass::Other;
}

int position_bonus(size_t pos, std::string_view target)
{
    if (pos == 0)
        return kBonusBoundary;

    const CharClass prev = classify(target[pos - 1]);
    const CharClass curr = classify(target[pos]);

    // After delimiter = boundary bonus
    if (prev == CharClass::Delimiter)
        return kBonusBoundary;

    // CamelCase transition: lower->upper or letter->digit or digit->letter
    if (prev == CharClass::Lower && curr == CharClass::Upper)
        return kBonusCamel123;
    if ((prev == CharClass::Lower || prev == CharClass::Upper) && curr == CharClass::Digit)
        return kBonusCamel123;
    if (prev == CharClass::Digit && (curr == CharClass::Lower || curr == CharClass::Upper))
        return kBonusCamel123;

    return 0;
}

} // namespace

FuzzyMatchResult fuzzy_match(std::string_view pattern, std::string_view target)
{
    FuzzyMatchResult result;

    if (pattern.empty())
    {
        result.matched = true;
        result.score = 0;
        return result;
    }

    if (target.empty())
        return result;

    // Greedy forward scan: find leftmost positions for all pattern chars
    std::vector<size_t> positions;
    positions.reserve(pattern.size());
    size_t t = 0;
    for (size_t p = 0; p < pattern.size(); ++p)
    {
        const char pc = static_cast<char>(std::tolower(static_cast<unsigned char>(pattern[p])));
        bool found = false;
        while (t < target.size())
        {
            if (std::tolower(static_cast<unsigned char>(target[t])) == pc)
            {
                positions.push_back(t);
                ++t;
                found = true;
                break;
            }
            ++t;
        }
        if (!found)
            return result; // not all chars matched
    }

    // Score the match
    int score = 0;
    for (size_t i = 0; i < positions.size(); ++i)
    {
        score += kScoreMatch;

        // Position bonus
        int bonus = position_bonus(positions[i], target);

        // Consecutive bonus
        if (i > 0 && positions[i] == positions[i - 1] + 1)
            bonus = std::max(bonus, kBonusConsecutive);

        // First character multiplier
        if (i == 0)
            bonus *= kBonusFirstCharMultiplier;

        score += bonus;

        // Gap penalty (between this match and the previous)
        if (i > 0)
        {
            const size_t gap = positions[i] - positions[i - 1] - 1;
            if (gap > 0)
                score += kScoreGapStart + static_cast<int>(gap - 1) * kScoreGapExtension;
        }
        else if (positions[0] > 0)
        {
            // Gap before first match
            score += kScoreGapStart + static_cast<int>(positions[0] - 1) * kScoreGapExtension;
        }
    }

    result.matched = true;
    result.score = score;
    result.positions = std::move(positions);
    return result;
}

} // namespace draxul

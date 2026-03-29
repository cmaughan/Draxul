#include <draxul/keybinding_parser.h>

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <draxul/app_config_types.h>
#include <draxul/events.h>
#include <draxul/perf_timing.h>
#include <string>
#include <vector>

namespace draxul
{

namespace
{

constexpr std::array<std::string_view, 9> kKnownGuiActions = {
    "toggle_diagnostics",
    "copy",
    "paste",
    "font_increase",
    "font_decrease",
    "font_reset",
    "open_file_dialog",
    "split_vertical",
    "split_horizontal",
};

ModifierFlags normalize_gui_modifiers(ModifierFlags mod)
{
    PERF_MEASURE();
    // Collapse each L/R pair: if either Left or Right of a group is set, treat as
    // the full group mask. This ensures Right Ctrl matches a binding requiring kModCtrl,
    // Right Shift matches kModShift, etc.
    ModifierFlags result = kModNone;
    if (mod & kModShift)
        result |= kModShift;
    if (mod & kModCtrl)
        result |= kModCtrl;
    if (mod & kModAlt)
        result |= kModAlt;
    if (mod & kModSuper)
        result |= kModSuper;
    return result;
}

bool is_known_gui_action(std::string_view action)
{
    return std::ranges::find(kKnownGuiActions, action) != kKnownGuiActions.end();
}

bool equals_ignore_case(std::string_view lhs, std::string_view rhs)
{
    PERF_MEASURE();
    if (lhs.size() != rhs.size())
        return false;

    for (size_t i = 0; i < lhs.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(lhs[i])) != std::tolower(static_cast<unsigned char>(rhs[i])))
            return false;
    }
    return true;
}

std::vector<std::string_view> split_key_combo(std::string_view combo)
{
    PERF_MEASURE();
    std::vector<std::string_view> trimmed_parts;
    size_t part_start = 0;
    while (part_start <= combo.size())
    {
        size_t plus = combo.find('+', part_start);
        std::string_view raw_part = plus == std::string_view::npos
            ? combo.substr(part_start)
            : combo.substr(part_start, plus - part_start);
        size_t begin = 0;
        size_t end = raw_part.size();
        while (begin < end && std::isspace(static_cast<unsigned char>(raw_part[begin])))
            ++begin;
        while (end > begin && std::isspace(static_cast<unsigned char>(raw_part[end - 1])))
            --end;
        if (begin == end)
            return {};
        trimmed_parts.push_back(raw_part.substr(begin, end - begin));
        if (plus == std::string_view::npos)
            break;
        part_start = plus + 1;
    }

    return trimmed_parts;
}

std::optional<ModifierFlags> parse_modifier_token(std::string_view token)
{
    PERF_MEASURE();
    if (equals_ignore_case(token, "ctrl") || equals_ignore_case(token, "control"))
        return kModCtrl;
    if (equals_ignore_case(token, "shift"))
        return kModShift;
    if (equals_ignore_case(token, "alt"))
        return kModAlt;
    if (equals_ignore_case(token, "super") || equals_ignore_case(token, "gui") || equals_ignore_case(token, "meta"))
        return kModSuper;
    return std::nullopt;
}

std::optional<int32_t> parse_key_token(std::string_view token)
{
    PERF_MEASURE();
    if (token.empty())
        return std::nullopt;

    if (token == "=" || equals_ignore_case(token, "equals"))
        return static_cast<int32_t>(SDLK_EQUALS);
    if (token == "-" || equals_ignore_case(token, "minus"))
        return static_cast<int32_t>(SDLK_MINUS);
    if (token == "+" || equals_ignore_case(token, "plus"))
        return static_cast<int32_t>(SDLK_PLUS);
    if (token == "|" || equals_ignore_case(token, "pipe"))
        return static_cast<int32_t>(SDLK_PIPE);

    std::string name(token);
    if (SDL_Keycode key = SDL_GetKeyFromName(name.c_str()); key != SDLK_UNKNOWN)
        return static_cast<int32_t>(key);

    for (char& ch : name)
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    if (SDL_Keycode key = SDL_GetKeyFromName(name.c_str()); key != SDLK_UNKNOWN)
        return static_cast<int32_t>(key);

    return std::nullopt;
}

std::string format_key_token(int32_t key)
{
    PERF_MEASURE();
    switch (key)
    {
    case SDLK_EQUALS:
        return "=";
    case SDLK_MINUS:
        return "-";
    case SDLK_PIPE:
        return "|";
    default:
        break;
    }

    const char* key_name = SDL_GetKeyName(static_cast<SDL_Keycode>(key));
    if (!key_name || !*key_name)
        return "Unknown";
    return key_name;
}

// Parse a single key+modifier half of a combo string (e.g. "ctrl+s" or "|").
// Returns {key, modifiers} or nullopt on failure.
std::optional<std::pair<int32_t, ModifierFlags>> parse_key_half(std::string_view half)
{
    PERF_MEASURE();
    // Trim leading/trailing whitespace.
    while (!half.empty() && std::isspace(static_cast<unsigned char>(half.front())))
        half.remove_prefix(1);
    while (!half.empty() && std::isspace(static_cast<unsigned char>(half.back())))
        half.remove_suffix(1);

    std::vector<std::string_view> parts = split_key_combo(half);
    if (parts.empty())
        return std::nullopt;

    ModifierFlags modifiers = kModNone;
    for (size_t i = 0; i + 1 < parts.size(); ++i)
    {
        auto modifier = parse_modifier_token(parts[i]);
        if (!modifier.has_value())
            return std::nullopt;
        modifiers |= *modifier;
    }

    auto key = parse_key_token(parts.back());
    if (!key.has_value())
        return std::nullopt;

    return std::make_pair(*key, normalize_gui_modifiers(modifiers));
}

} // namespace

std::optional<GuiKeybinding> parse_gui_keybinding(std::string_view action, std::string_view combo)
{
    PERF_MEASURE();
    if (!is_known_gui_action(action))
        return std::nullopt;

    // Chord syntax: "Ctrl+S, |" -- split on the first comma.
    if (const size_t comma = combo.find(','); comma != std::string_view::npos)
    {
        const std::string_view prefix_half = combo.substr(0, comma);
        const std::string_view action_half = combo.substr(comma + 1);
        auto prefix = parse_key_half(prefix_half);
        auto act = parse_key_half(action_half);
        if (!prefix || !act)
            return std::nullopt;
        return GuiKeybinding{ std::string(action), prefix->first, prefix->second, act->first, act->second };
    }

    // Single-key binding (no prefix).
    auto half = parse_key_half(combo);
    if (!half)
        return std::nullopt;
    return GuiKeybinding{ std::string(action), 0, kModNone, half->first, half->second };
}

std::string format_gui_keybinding_combo(int32_t key, ModifierFlags modifiers)
{
    PERF_MEASURE();
    std::string combo;
    ModifierFlags normalized = normalize_gui_modifiers(modifiers);
    auto append_modifier = [&](ModifierFlags flag, std::string_view name) {
        if ((normalized & flag) == 0)
            return;
        if (!combo.empty())
            combo += '+';
        combo += name;
    };

    append_modifier(kModCtrl, "Ctrl");
    append_modifier(kModShift, "Shift");
    append_modifier(kModAlt, "Alt");
    append_modifier(kModSuper, "Super");

    if (!combo.empty())
        combo += '+';
    combo += format_key_token(key);
    return combo;
}

bool gui_keybinding_matches(const GuiKeybinding& binding, const KeyEvent& event)
{
    PERF_MEASURE();
    ModifierFlags expected_modifiers = normalize_gui_modifiers(binding.modifiers);
    ModifierFlags event_modifiers = normalize_gui_modifiers(event.mod);

    if (event.keycode == binding.key && event_modifiers == expected_modifiers)
        return true;

    // Preserve the historical Ctrl+= / Ctrl+Plus zoom-in behavior with a single canonical binding.
    return binding.key == static_cast<int32_t>(SDLK_EQUALS)
        && event.keycode == static_cast<int>(SDLK_PLUS)
        && expected_modifiers == kModCtrl
        && event_modifiers == (kModCtrl | kModShift);
}

bool gui_prefix_matches(const GuiKeybinding& binding, const KeyEvent& event)
{
    PERF_MEASURE();
    if (binding.prefix_key == 0)
        return false;
    return event.keycode == binding.prefix_key
        && normalize_gui_modifiers(event.mod) == normalize_gui_modifiers(binding.prefix_modifiers);
}

} // namespace draxul

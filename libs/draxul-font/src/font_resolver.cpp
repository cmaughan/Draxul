#include "font_resolver.h"

#include <draxul/log.h>
#include <draxul/text_service.h>

#include <filesystem>
#include <utility>

namespace draxul
{

namespace detail
{

std::vector<std::string> default_fallback_font_candidates()
{
#ifdef _WIN32
    const char* windir = std::getenv("WINDIR");
    std::string windows_dir = windir ? windir : "C:\\Windows";
    return {
        windows_dir + "\\Fonts\\seguiemj.ttf",
        windows_dir + "\\Fonts\\seguisym.ttf",
        windows_dir + "\\Fonts\\YuGothR.ttc",
        windows_dir + "\\Fonts\\YuGothM.ttc",
        windows_dir + "\\Fonts\\meiryo.ttc",
        windows_dir + "\\Fonts\\msgothic.ttc",
        windows_dir + "\\Fonts\\msyh.ttc",
        windows_dir + "\\Fonts\\simsun.ttc",
    };
#elif defined(__APPLE__)
    return {
        "/System/Library/Fonts/Apple Color Emoji.ttc",
        "/System/Library/Fonts/Apple Symbols.ttf",
        // Apple Symbols lacks several technical symbols (e.g. U+23F5 ⏵ used
        // by Claude Code's status line); STIX Two Math is the only system
        // font that covers them.
        "/System/Library/Fonts/Supplemental/STIXTwoMath.otf",
        "/System/Library/Fonts/Apple Braille.ttf",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "/System/Library/Fonts/Hiragino Sans GB.ttc",
        "/System/Library/Fonts/Supplemental/Songti.ttc",
    };
#else
    return {
        "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf",
        "/usr/share/fonts/truetype/noto/NotoEmoji-Regular.ttf",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    };
#endif
}

std::vector<std::string> default_primary_font_candidates()
{
#ifdef _WIN32
    const char* windir = std::getenv("WINDIR");
    const char* local_app_data = std::getenv("LOCALAPPDATA");
    std::string windows_dir = windir ? windir : "C:\\Windows";
    std::string local_fonts = local_app_data
        ? (std::string(local_app_data) + "\\Microsoft\\Windows\\Fonts\\")
        : "";

    std::vector<std::string> candidates;
    if (!local_fonts.empty())
    {
        candidates.push_back(local_fonts + "JetBrainsMonoNerdFont-Regular.ttf");
        candidates.push_back(local_fonts + "JetBrainsMonoNerdFontMono-Regular.ttf");
    }
    candidates.push_back(windows_dir + "\\Fonts\\JetBrainsMonoNerdFont-Regular.ttf");
    candidates.push_back(windows_dir + "\\Fonts\\JetBrainsMonoNerdFontMono-Regular.ttf");
    candidates.push_back(
        windows_dir + "\\Fonts\\JetBrains Mono Regular Nerd Font Complete Windows Compatible.ttf");
    candidates.push_back(
        windows_dir + "\\Fonts\\JetBrains Mono Regular Nerd Font Complete Mono Windows Compatible.ttf");
    candidates.push_back("fonts/JetBrainsMonoNerdFont-Regular.ttf");
    return candidates;
#else
    return { "fonts/JetBrainsMonoNerdFont-Regular.ttf" };
#endif
}

std::string first_existing_path(const std::vector<std::string>& candidates)
{
    for (const auto& path : candidates)
    {
        if (std::filesystem::exists(path))
            return path;
    }
    return {};
}

std::string auto_detect_style_path(const std::string& regular_path, FontStyle style)
{
    if (style == FontStyle::Regular)
        return {};

    const std::string suffix = font_style_file_suffix(style);
    const std::pair<std::string, std::string> replacements[] = {
        { "-Regular.", "-" + suffix + "." },
        { "_Regular.", "_" + suffix + "." },
        { "-Regular-", "-" + suffix + "-" },
    };
    for (const auto& [from, to] : replacements)
    {
        auto pos = regular_path.rfind(from);
        if (pos != std::string::npos)
        {
            std::string candidate = regular_path.substr(0, pos) + to + regular_path.substr(pos + from.size());
            if (std::filesystem::exists(candidate))
                return candidate;
        }
    }
    return {};
}

namespace
{

// Maps each style variant to its explicit TextServiceConfig path override.
const std::string& config_style_path(const TextServiceConfig& config, FontStyle style)
{
    switch (style)
    {
    case FontStyle::Bold:
        return config.bold_font_path;
    case FontStyle::Italic:
        return config.italic_font_path;
    case FontStyle::BoldItalic:
        return config.bold_italic_font_path;
    default:
        return config.font_path;
    }
}

void resize_styled_font(FontResolver::StyledFont& slot, float point_size, bool enable_ligatures)
{
    slot.font.set_point_size(point_size);
    slot.shaper.initialize(slot.font.hb_font(), enable_ligatures);
    slot.unligated_shaper.initialize(slot.font.hb_font(), false);
}

} // namespace

} // namespace detail

bool FontResolver::initialize(const TextServiceConfig& config, float point_size, float display_ppi)
{
    shutdown();

    config_ = &config;
    display_ppi_ = display_ppi;
    warnings_.clear();

    auto& regular = style(FontStyle::Regular);
    if (config.font_path.empty())
    {
        regular.path = detail::first_existing_path(detail::default_primary_font_candidates());
    }
    else
    {
        regular.path = config.font_path;
        if (!std::filesystem::exists(regular.path))
        {
            DRAXUL_LOG_WARN(LogCategory::Font, "Configured font path does not exist: '%s'", regular.path.c_str());
            warnings_.push_back("Configured font path does not exist: " + regular.path);
        }
    }
    if (regular.path.empty())
        regular.path = "fonts/JetBrainsMonoNerdFont-Regular.ttf";

    if (!regular.font.initialize(regular.path, point_size, display_ppi))
    {
        regular.font.shutdown();
        return false;
    }

    regular.shaper.initialize(regular.font.hb_font(), config.enable_ligatures);
    regular.unligated_shaper.initialize(regular.font.hb_font(), false);
    regular.loaded = true;

    for (FontStyle variant : FONT_STYLE_VARIANTS)
        load_style_variant(variant, point_size);

    load_fallback_fonts();
    return true;
}

void FontResolver::load_style_variant(FontStyle variant, float point_size)
{
    const char* display_name = font_style_display_name(variant);

    std::string path = detail::config_style_path(*config_, variant);
    if (path.empty())
        path = detail::auto_detect_style_path(font_path(), variant);

    if (path.empty())
    {
        DRAXUL_LOG_WARN(LogCategory::Font, "No %s font found for: %s",
            font_style_lower_name(variant), font_path().c_str());
        warnings_.push_back(
            std::string("No ") + font_style_lower_name(variant) + " font variant found; using regular");
        return;
    }
    if (path == font_path())
    {
        DRAXUL_LOG_DEBUG(LogCategory::Font, "%s font path same as regular, skipping", display_name);
        return;
    }

    auto& slot = style(variant);
    if (!slot.font.initialize(path, point_size, display_ppi_))
    {
        slot.font.shutdown();
        DRAXUL_LOG_WARN(LogCategory::Font, "%s font not found: %s", display_name, path.c_str());
        warnings_.push_back(std::string(display_name) + " font not found: " + path);
        return;
    }

    slot.shaper.initialize(slot.font.hb_font(), config_->enable_ligatures);
    slot.unligated_shaper.initialize(slot.font.hb_font(), false);
    slot.loaded = true;
    slot.path = path;
    DRAXUL_LOG_DEBUG(LogCategory::Font, "%s font loaded: %s", display_name, path.c_str());
}

void FontResolver::shutdown()
{
    for (auto& fallback : fallbacks_)
    {
        if (!fallback.loaded)
            continue;
        fallback.shaper.shutdown();
        fallback.unligated_shaper.shutdown();
        fallback.font.shutdown();
    }
    fallbacks_.clear();

    for (auto& slot : styles_)
    {
        slot.shaper.shutdown();
        slot.unligated_shaper.shutdown();
        slot.font.shutdown();
        slot.path.clear();
        slot.loaded = false;
        slot.failed = false;
    }

    config_ = nullptr;
}

bool FontResolver::set_point_size(float point_size)
{
    if (!primary().set_point_size(point_size))
        return false;

    auto& regular = style(FontStyle::Regular);
    regular.shaper.initialize(regular.font.hb_font(), config_->enable_ligatures);
    regular.unligated_shaper.initialize(regular.font.hb_font(), false);

    for (auto& fallback : fallbacks_)
    {
        if (!fallback.loaded)
            continue;
        detail::resize_styled_font(fallback, point_size, config_->enable_ligatures);
    }
    for (FontStyle variant : FONT_STYLE_VARIANTS)
    {
        auto& slot = style(variant);
        if (!slot.loaded)
            continue;
        detail::resize_styled_font(slot, point_size, config_->enable_ligatures);
    }
    return true;
}

bool FontResolver::ensure_loaded(size_t index)
{
    auto& fb = fallbacks_[index];
    if (fb.loaded)
        return true;
    if (fb.failed)
        return false;

    if (!fb.font.initialize(fb.path, primary().point_size(), display_ppi_))
    {
        DRAXUL_LOG_WARN(LogCategory::Font, "Fallback font failed to load: %s", fb.path.c_str());
        fb.failed = true;
        return false;
    }

    fb.shaper.initialize(fb.font.hb_font(), config_->enable_ligatures);
    fb.unligated_shaper.initialize(fb.font.hb_font(), false);
    fb.loaded = true;
    DRAXUL_LOG_DEBUG(LogCategory::Font, "Fallback font loaded (on demand): %s", fb.path.c_str());
    return true;
}

void FontResolver::load_fallback_fonts()
{
    fallbacks_.clear();

    std::vector<std::string> candidates = config_->fallback_paths.empty()
        ? detail::default_fallback_font_candidates()
        : config_->fallback_paths;

    fallbacks_.reserve(candidates.size());
    for (const auto& path : candidates)
    {
        if (path == font_path())
        {
            DRAXUL_LOG_DEBUG(LogCategory::Font, "Fallback candidate skipped (same as primary): %s", path.c_str());
            continue;
        }
        if (!std::filesystem::exists(path))
        {
            DRAXUL_LOG_DEBUG(LogCategory::Font, "Fallback candidate not found, skipping: %s", path.c_str());
            continue;
        }

        fallbacks_.emplace_back();
        fallbacks_.back().path = path;
        DRAXUL_LOG_DEBUG(LogCategory::Font, "Fallback font registered (deferred load): %s", path.c_str());
    }
}

} // namespace draxul

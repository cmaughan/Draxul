#include <draxul/config_document.h>
#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <draxul/toml_support.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace draxul
{

namespace
{

// Complete AppConfig ownership inventory. Only these app-owned keys are
// replaced during a merge; unlisted top-level tables belong to optional host
// modules and are deliberately preserved verbatim in the TOML document tree.
constexpr std::array<std::string_view, 25> kCoreTopLevelKeys = {
    "window_width",
    "window_height",
    "font_size",
    "atlas_size",
    "enable_ligatures",
    "smooth_scroll",
    "scroll_speed",
    "scrollback_lines",
    "palette_bg_alpha",
    "focus_border_width",
    "enable_toast_notifications",
    "toast_duration_s",
    "show_pane_status",
    "chord_timeout_ms",
    "chord_indicator_fade_ms",
    "weather_location",
    "font_path",
    "bold_font_path",
    "italic_font_path",
    "bold_italic_font_path",
    "fallback_paths",
    "keybindings",
    "terminal",
    "chrome",
    "markdown",
};

std::vector<std::string_view> split_dotted_path(std::string_view dotted_path)
{
    PERF_MEASURE();
    std::vector<std::string_view> parts;
    while (!dotted_path.empty())
    {
        const size_t dot = dotted_path.find('.');
        if (dot == std::string_view::npos)
        {
            parts.push_back(dotted_path);
            break;
        }
        parts.push_back(dotted_path.substr(0, dot));
        dotted_path.remove_prefix(dot + 1);
    }
    return parts;
}

} // namespace

std::filesystem::path ConfigDocument::default_path()
{
    PERF_MEASURE();
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    if (!appdata || appdata[0] == '\0')
    {
        DRAXUL_LOG_WARN(LogCategory::App, "APPDATA is not set or empty; using fallback config path");
        appdata = nullptr;
    }
    std::filesystem::path base = appdata ? appdata : ".";
    return base / "draxul" / "config.toml";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (!home || home[0] == '\0')
    {
        DRAXUL_LOG_WARN(LogCategory::App, "HOME is not set or empty; using fallback config path");
        home = nullptr;
    }
    std::filesystem::path base = home ? home : ".";
    return base / "Library" / "Application Support" / "draxul" / "config.toml";
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    if (xdg && xdg[0] == '\0')
    {
        DRAXUL_LOG_WARN(LogCategory::App, "XDG_CONFIG_HOME is empty; using fallback config path");
        xdg = nullptr;
    }
    if (home && home[0] == '\0')
    {
        DRAXUL_LOG_WARN(LogCategory::App, "HOME is empty; using fallback config path");
        home = nullptr;
    }
    std::filesystem::path base = xdg ? xdg : (home ? std::filesystem::path(home) / ".config" : std::filesystem::path("."));
    return base / "draxul" / "config.toml";
#endif
}

ConfigDocument ConfigDocument::load()
{
    return load_from_path(default_path());
}

ConfigDocument ConfigDocument::load_from_path(const std::filesystem::path& path)
{
    PERF_MEASURE();
    auto loaded = load_config_document_from_path_checked(path);
    if (loaded)
        return std::move(*loaded);
    DRAXUL_LOG_WARN(LogCategory::App, "%s", loaded.error().message.c_str());
    return {};
}

Result<ConfigDocument, Error> load_config_document_from_path_checked(
    const std::filesystem::path& path)
{
    PERF_MEASURE();
    ConfigDocument document;
    try
    {
        if (!std::filesystem::exists(path))
            return Result<ConfigDocument, Error>::ok(std::move(document));

        std::string parse_error;
        auto parsed = toml_support::parse_file(path, &parse_error);
        if (!parsed)
        {
            if (parse_error == "Unable to open TOML file")
                return Result<ConfigDocument, Error>::err(Error::config_load(
                    "Failed to open config document for reading: " + path.string()));
            return Result<ConfigDocument, Error>::err(Error::config_parse(
                "Failed to parse config document from " + path.string() + ": " + parse_error));
        }

        document.document_ = std::move(*parsed);
        return Result<ConfigDocument, Error>::ok(std::move(document));
    }
    catch (const std::filesystem::filesystem_error& ex)
    {
        return Result<ConfigDocument, Error>::err(Error::config_load(
            "Failed to load config document from " + path.string() + ": " + ex.what()));
    }
    catch (const std::ios_base::failure& ex)
    {
        return Result<ConfigDocument, Error>::err(Error::config_load(
            "Failed to load config document from " + path.string() + ": " + ex.what()));
    }
}

void ConfigDocument::save() const
{
    save_to_path(default_path());
}

void ConfigDocument::save_to_path(const std::filesystem::path& path) const
{
    PERF_MEASURE();
    try
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::trunc);
        if (!out)
        {
            DRAXUL_LOG_WARN(LogCategory::App, "Failed to open config document for writing: %s", path.string().c_str());
            return;
        }

        out << document_ << '\n';
        if (!out)
            DRAXUL_LOG_WARN(LogCategory::App, "Failed to write config document to %s", path.string().c_str());
    }
    catch (const std::filesystem::filesystem_error& ex)
    {
        DRAXUL_LOG_WARN(LogCategory::App, "Failed to save config document to %s: %s", path.string().c_str(), ex.what());
    }
    catch (const std::ios_base::failure& ex)
    {
        DRAXUL_LOG_WARN(LogCategory::App, "Failed to save config document to %s: %s", path.string().c_str(), ex.what());
    }
}

const toml::table* ConfigDocument::find_table(std::string_view dotted_path) const
{
    PERF_MEASURE();
    const toml::table* current = &document_;
    for (const std::string_view part : split_dotted_path(dotted_path))
    {
        if (part.empty())
            return nullptr;
        const toml::node_view<const toml::node> node = (*current)[part];
        current = node.as_table();
        if (!current)
            return nullptr;
    }
    return current;
}

toml::table& ConfigDocument::ensure_table(std::string_view dotted_path)
{
    PERF_MEASURE();
    toml::table* current = &document_;
    for (const std::string_view part : split_dotted_path(dotted_path))
    {
        if (part.empty())
            continue;
        toml::node_view<toml::node> node = (*current)[part];
        toml::table* child = node.as_table();
        if (!child)
        {
            current->insert_or_assign(std::string(part), toml::table{});
            child = (*current)[part].as_table();
        }
        current = child;
    }
    return *current;
}

void ConfigDocument::merge_core_config(const AppConfig& config)
{
    PERF_MEASURE();
    std::string parse_error;
    auto parsed = toml_support::parse_document(config.serialize(), &parse_error);
    if (!parsed)
    {
        DRAXUL_LOG_WARN(LogCategory::App, "Failed to merge core config into document: %s", parse_error.c_str());
        return;
    }

    for (std::string_view key : kCoreTopLevelKeys)
    {
        document_.erase(key);
        if (const toml::node* node = parsed->get(key))
            document_.insert_or_assign(std::string(key), *node);
    }
}

} // namespace draxul

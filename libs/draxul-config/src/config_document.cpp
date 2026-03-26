#include <draxul/config_document.h>
#include <draxul/log.h>
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

constexpr std::array<std::string_view, 14> kCoreTopLevelKeys = {
    "window_width",
    "window_height",
    "font_size",
    "atlas_size",
    "enable_ligatures",
    "smooth_scroll",
    "scroll_speed",
    "font_path",
    "bold_font_path",
    "italic_font_path",
    "bold_italic_font_path",
    "fallback_paths",
    "keybindings",
    "terminal",
};

std::vector<std::string_view> split_dotted_path(std::string_view dotted_path)
{
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
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    std::filesystem::path base = appdata ? appdata : ".";
    return base / "draxul" / "config.toml";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    std::filesystem::path base = home ? home : ".";
    return base / "Library" / "Application Support" / "draxul" / "config.toml";
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
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
    ConfigDocument document;
    try
    {
        if (!std::filesystem::exists(path))
            return document;

        std::string parse_error;
        auto parsed = toml_support::parse_file(path, &parse_error);
        if (!parsed)
        {
            if (parse_error == "Unable to open TOML file")
                DRAXUL_LOG_WARN(LogCategory::App, "Failed to open config document for reading: %s", path.string().c_str());
            else
                DRAXUL_LOG_WARN(LogCategory::App, "Failed to parse config document from %s: %s", path.string().c_str(), parse_error.c_str());
            return document;
        }

        document.document_ = std::move(*parsed);
        return document;
    }
    catch (const std::filesystem::filesystem_error& ex)
    {
        DRAXUL_LOG_WARN(LogCategory::App, "Failed to load config document from %s: %s", path.string().c_str(), ex.what());
        return document;
    }
    catch (const std::ios_base::failure& ex)
    {
        DRAXUL_LOG_WARN(LogCategory::App, "Failed to load config document from %s: %s", path.string().c_str(), ex.what());
        return document;
    }
}

void ConfigDocument::save() const
{
    save_to_path(default_path());
}

void ConfigDocument::save_to_path(const std::filesystem::path& path) const
{
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

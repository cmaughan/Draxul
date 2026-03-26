#pragma once

#include <draxul/app_config_types.h>
#include <filesystem>
#include <string_view>
#include <toml++/toml.hpp>

namespace draxul
{

class ConfigDocument
{
public:
    static ConfigDocument load();
    static ConfigDocument load_from_path(const std::filesystem::path& path);
    static std::filesystem::path default_path();

    void save() const;
    void save_to_path(const std::filesystem::path& path) const;

    [[nodiscard]] const toml::table& root() const
    {
        return document_;
    }

    [[nodiscard]] toml::table& root()
    {
        return document_;
    }

    [[nodiscard]] const toml::table* find_table(std::string_view dotted_path) const;
    [[nodiscard]] toml::table& ensure_table(std::string_view dotted_path);
    void merge_core_config(const AppConfig& config);

private:
    toml::table document_;
};

} // namespace draxul

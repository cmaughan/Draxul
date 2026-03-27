#pragma once

#include <glm/vec3.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>
#include <vector>

namespace draxul::toml_support
{

inline std::optional<toml::table> parse_document(std::string_view content, std::string* error_message = nullptr)
{
    try
    {
        return toml::parse(content);
    }
    catch (const toml::parse_error& ex)
    {
        if (error_message)
        {
            std::ostringstream out;
            out << ex;
            *error_message = out.str();
        }
        return std::nullopt;
    }
}

inline std::optional<toml::table> parse_file(const std::filesystem::path& path, std::string* error_message = nullptr)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        if (error_message)
            *error_message = "Unable to open TOML file";
        return std::nullopt;
    }

    std::string content{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>{} };
    return parse_document(content, error_message);
}

inline std::optional<std::string> get_string(const toml::table& table, std::string_view key)
{
    if (auto value = table[key].value<std::string_view>())
        return std::string(*value);
    return std::nullopt;
}

inline std::optional<std::vector<std::string>> get_string_array(const toml::table& table, std::string_view key)
{
    const auto* arr = table[key].as_array();
    if (!arr)
        return std::nullopt;

    std::vector<std::string> out;
    out.reserve(arr->size());
    for (const auto& value : *arr)
    {
        auto str = value.value<std::string_view>();
        if (!str)
            return std::nullopt;
        out.emplace_back(*str);
    }
    return out;
}

inline std::optional<int64_t> get_int(const toml::table& table, std::string_view key)
{
    return table[key].value<int64_t>();
}

inline std::optional<double> get_double(const toml::table& table, std::string_view key)
{
    return table[key].value<double>();
}

inline std::optional<bool> get_bool(const toml::table& table, std::string_view key)
{
    return table[key].value<bool>();
}

inline std::optional<glm::vec3> get_vec3(const toml::table& table, std::string_view key)
{
    const auto* arr = table[key].as_array();
    if (!arr || arr->size() != 3)
        return std::nullopt;

    glm::vec3 value{ 0.0f };
    for (size_t i = 0; i < 3; ++i)
    {
        if (auto parsed = (*arr)[i].value<double>())
        {
            value[i] = static_cast<float>(*parsed);
            continue;
        }
        if (auto parsed = (*arr)[i].value<int64_t>())
        {
            value[i] = static_cast<float>(*parsed);
            continue;
        }
        return std::nullopt;
    }
    return value;
}

inline toml::array make_array(const glm::vec3& value)
{
    toml::array array;
    array.push_back(static_cast<double>(value.x));
    array.push_back(static_cast<double>(value.y));
    array.push_back(static_cast<double>(value.z));
    return array;
}

inline void insert_vec3(toml::table& table, std::string_view key, const glm::vec3& value)
{
    table.insert_or_assign(std::string(key), make_array(value));
}

} // namespace draxul::toml_support

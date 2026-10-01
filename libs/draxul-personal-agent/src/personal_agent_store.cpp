#include <draxul/personal_agent_store.h>
#include <draxul/unicode.h>

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <stdexcept>

namespace draxul
{
namespace
{
std::string utf8_path(const std::filesystem::path& path)
{
    const auto value = path.u8string();
    return { reinterpret_cast<const char*>(value.data()), value.size() };
}

void require_contained(const std::filesystem::path& root, const std::filesystem::path& path)
{
    const auto relative = std::filesystem::canonical(path).lexically_relative(root);
    if (relative.empty() || relative.is_absolute()
        || std::ranges::any_of(relative, [](const auto& part) { return part == ".."; }))
        throw std::runtime_error("Path escapes the collection root.");
}

std::string read_file(const std::filesystem::path& root,
    const std::filesystem::path& path, size_t limit)
{
    require_contained(root, path);
    if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > limit)
        throw std::runtime_error("Required file is not regular or exceeds its size limit.");
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Required file is not locally readable.");
    std::string contents(limit + 1, '\0');
    input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    contents.resize(static_cast<size_t>(input.gcount()));
    if (input.bad() || contents.size() > limit || contents.find('\0') != std::string::npos
        || utf8_validated_prefix_length(contents, contents.size()) != contents.size())
        throw std::runtime_error("Required file is unreadable, oversized or not valid UTF-8 text.");
    return contents;
}

void check_keys(const toml::table& table, std::initializer_list<std::string_view> keys)
{
    for (const auto& [key, value] : table)
    {
        if (std::ranges::find(keys, key.str()) == keys.end())
            throw std::runtime_error("Unknown manifest field: " + std::string(key.str()));
    }
    if (!table["schema_version"].is_integer()
        || table["schema_version"].value<int64_t>() != kPersonalAgentSchemaVersion)
        throw std::runtime_error("Unsupported or missing schema_version; expected 1.");
}

std::string required_text(const toml::table& table, std::string_view key)
{
    const auto value = table[key].value<std::string>();
    if (!value || value->empty() || value->size() > 160
        || std::ranges::any_of(*value, [](unsigned char byte) { return byte < 32 || byte == 127; }))
        throw std::runtime_error("Missing or invalid field: " + std::string(key));
    return *value;
}

void reject_conflicts(const std::filesystem::path& directory, std::string_view prefix)
{
    size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory))
    {
        if (++count > 256)
            throw std::runtime_error("Manifest directory exceeds the entry limit.");
        std::string name = utf8_path(entry.path().filename());
        std::ranges::transform(name, name.begin(), [](unsigned char byte) {
            return static_cast<char>(std::tolower(byte));
        });
        if (name.starts_with(prefix) && name.find("conflicted copy") != std::string::npos)
            throw std::runtime_error("A conflicted copy requires reconciliation.");
    }
}
}

bool valid_personal_agent_id(std::string_view value)
{
    return !value.empty() && value.size() <= 64
        && std::ranges::all_of(value, [](unsigned char byte) {
               return (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') || byte == '-';
           });
}

PersonalAgentSnapshot load_personal_agents(const std::filesystem::path& configured_root,
    const PersonalAgentSnapshot& previous)
{
    PersonalAgentSnapshot result;
    result.root = utf8_path(configured_root);
    if (configured_root.empty())
        return result;
    try
    {
        if (!configured_root.is_absolute())
            throw std::runtime_error("The collection root must be an absolute path.");
        const auto root = std::filesystem::canonical(configured_root);
        reject_conflicts(root, "collection");
        const auto manifest = toml::parse(read_file(root, root / "collection.toml", 4096));
        check_keys(manifest, { "schema_version", "id", "name" });
        result.collection_id = required_text(manifest, "id");
        result.name = required_text(manifest, "name");
        if (!valid_personal_agent_id(result.collection_id))
            throw std::runtime_error("Invalid collection identity.");
        const auto directory = root / "agents";
        require_contained(root, directory);
        std::vector<std::filesystem::path> paths;
        for (const auto& entry : std::filesystem::directory_iterator(directory))
        {
            if (paths.size() >= kPersonalAgentLimit)
                throw std::runtime_error("The collection exceeds the 32-definition limit.");
            paths.push_back(entry.path());
        }
        std::ranges::sort(paths);
        for (const auto& path : paths)
        {
            PersonalAgentDefinition definition;
            definition.id = utf8_path(path.filename());
            if (utf8_validated_prefix_length(definition.id, definition.id.size()) != definition.id.size())
                throw std::runtime_error("A folder name is not valid UTF-8.");
            definition.name = definition.id;
            try
            {
                if (!valid_personal_agent_id(definition.id))
                    throw std::runtime_error("Invalid folder identity or conflicted folder.");
                require_contained(root, path);
                reject_conflicts(path, "agent");
                reject_conflicts(path, "instructions");
                const auto text = read_file(root, path / "agent.toml", 4096);
                const auto table = toml::parse(text);
                check_keys(table, { "schema_version", "id", "name", "profile", "model", "revision", "enabled", "interval_seconds" });
                if (required_text(table, "id") != definition.id)
                    throw std::runtime_error("Manifest identity must match its folder.");
                definition.name = required_text(table, "name");
                definition.profile = required_text(table, "profile");
                definition.model = required_text(table, "model");
                const auto revision = table["revision"].value<int64_t>();
                const auto enabled = table["enabled"].value<bool>();
                const auto interval = table["interval_seconds"].value<int64_t>();
                if (!table["revision"].is_integer() || !table["interval_seconds"].is_integer()
                    || !revision || *revision <= 0 || !enabled || !interval || *interval < 60 || *interval > 31536000)
                    throw std::runtime_error("Expected positive revision, enabled boolean and interval_seconds from 60 to 31536000.");
                definition.revision = static_cast<uint64_t>(*revision);
                definition.enabled = *enabled;
                definition.interval_seconds = static_cast<int>(*interval);
                definition.instructions = read_file(root, path / "instructions.md", kPersonalInstructionsLimit);
                if (definition.instructions.empty())
                    throw std::runtime_error("Instructions must not be empty.");
                if (read_file(root, path / "agent.toml", 4096) != text)
                    throw std::runtime_error("Manifest changed during the scan; waiting for sync.");
            }
            catch (const std::exception& error)
            {
                if (previous.collection_id == result.collection_id)
                {
                    const auto found = std::ranges::find(previous.agents, definition.id, &PersonalAgentDefinition::id);
                    if (found != previous.agents.end())
                        definition = *found;
                }
                const std::string message = error.what();
                definition.error = message.substr(0, utf8_validated_prefix_length(message, 512));
                if (definition.error.empty())
                    definition.error = "Definition could not be read.";
            }
            result.agents.push_back(std::move(definition));
        }
        if (previous.collection_id == result.collection_id)
        {
            for (const auto& old : previous.agents)
            {
                if (result.agents.size() >= kPersonalAgentLimit)
                    break;
                if (std::ranges::find(result.agents, old.id, &PersonalAgentDefinition::id) == result.agents.end())
                {
                    auto missing = old;
                    missing.error = "Definition missing; retained for inspection while sync settles.";
                    result.agents.push_back(std::move(missing));
                }
            }
        }
    }
    catch (const std::exception& error)
    {
        result = previous.root == result.root ? previous : PersonalAgentSnapshot{};
        result.root = utf8_path(configured_root);
        const std::string message = error.what();
        result.error = message.substr(0, utf8_validated_prefix_length(message, 512));
        if (result.error.empty())
            result.error = "Collection could not be read.";
    }
    return result;
}

}

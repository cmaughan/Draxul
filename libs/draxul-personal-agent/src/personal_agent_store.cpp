#include <draxul/personal_agent_store.h>
#include <draxul/unicode.h>

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <stdexcept>
#include <sstream>
#include <random>
#include <limits>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

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
        throw std::runtime_error("Unsupported or missing schema_version; expected 3.");
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
                check_keys(table, { "schema_version", "id", "name", "profile", "revision" });
                if (required_text(table, "id") != definition.id)
                    throw std::runtime_error("Manifest identity must match its folder.");
                definition.name = required_text(table, "name");
                definition.profile = required_text(table, "profile");
                const auto revision = table["revision"].value<int64_t>();
                if (!revision || *revision <= 0)
                    throw std::runtime_error("Expected a positive metadata revision.");
                definition.revision = static_cast<uint64_t>(*revision);
                if (std::filesystem::exists(path / "instructions.md"))
                    definition.instructions = read_file(root, path / "instructions.md", kPersonalInstructionsLimit);
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
                if (std::filesystem::exists(root / ".deleted" / old.id)) continue;
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

namespace draxul
{
std::string personal_unique_id()
{
    std::random_device random;
    std::ostringstream out;
    out << std::hex << random() << random() << random() << random();
    return out.str();
}

std::string personal_read_bounded(const std::filesystem::path& root,
    const std::filesystem::path& path, size_t limit)
{
    return read_file(std::filesystem::canonical(root), path, limit);
}

void personal_write_atomic(const std::filesystem::path& path, std::string_view text)
{
    const auto temporary = path.parent_path() / (".staging-" + personal_unique_id());
    try
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        output.flush();
        if (!output)
            throw std::runtime_error("Cannot write personal agent file.");
        output.close();
        if (!output)
            throw std::runtime_error("Cannot close personal agent file.");
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot publish personal agent file.");
#else
        std::filesystem::rename(temporary, path);
#endif
    }
    catch (...)
    {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

void validate_personal_agent(const PersonalAgentDefinition& d)
{
    auto valid_text = [](std::string_view text) {
        return !text.empty() && text.size() <= 160
            && text.find('\0') == std::string_view::npos
            && utf8_validated_prefix_length(text, text.size()) == text.size()
            && std::ranges::none_of(text, [](unsigned char ch) { return ch < 32 || ch == 127; });
    };
    if (!valid_personal_agent_id(d.id) || !valid_text(d.name)
        || !valid_text(d.profile)
        || d.instructions.size() > kPersonalInstructionsLimit
        || d.instructions.find('\0') != std::string::npos
        || utf8_validated_prefix_length(d.instructions, d.instructions.size()) != d.instructions.size())
        throw std::runtime_error("Invalid definition: check identity, name, profile and instructions.");
}

PersonalAgentDefinition save_personal_agent(const std::filesystem::path& configured_root,
    std::string_view collection_id, PersonalAgentDefinition candidate,
    const std::optional<PersonalAgentDefinition>& expected)
{
    validate_personal_agent(candidate);
    const auto root = std::filesystem::canonical(configured_root);
    const auto snapshot = load_personal_agents(root);
    if (!snapshot.error.empty() || snapshot.collection_id != collection_id)
        throw std::runtime_error("Collection unavailable or changed; reload before saving.");
    const auto found = std::ranges::find(snapshot.agents, candidate.id, &PersonalAgentDefinition::id);
    if (expected)
    {
        if (found == snapshot.agents.end() || !found->error.empty() || *found != *expected)
            throw std::runtime_error("Definition changed or is invalid; reload before saving. Your draft is retained.");
        if (expected->revision >= static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
            throw std::runtime_error("Definition revision exhausted.");
        candidate.revision = expected->revision + 1;
    }
    else
    {
        if (found != snapshot.agents.end() || snapshot.agents.size() >= kPersonalAgentLimit)
            throw std::runtime_error("Identity already exists or collection is full.");
        candidate.revision = 1;
    }
    candidate.error.clear();
    const auto destination = root / "agents" / candidate.id;
    const auto staging = root / (".agent-" + personal_unique_id());
    const auto directory = expected ? destination : staging;
    if (expected)
        require_contained(root, directory);
    else
        std::filesystem::create_directory(directory);
    const std::string filename = "instructions.md";
    try
    {
        if (!expected)
        {
            personal_write_atomic(directory / filename, candidate.instructions);
            std::filesystem::create_directory(directory / "data");
        }
        toml::table manifest{
            {"schema_version", kPersonalAgentSchemaVersion}, {"id", candidate.id},
            {"name", candidate.name}, {"profile", candidate.profile},
            {"revision", static_cast<int64_t>(candidate.revision)}};
        std::ostringstream text;
        text << manifest;
        // Recheck immediately before publication, including external instructions.
        const auto latest = load_personal_agents(root);
        const auto current = std::ranges::find(latest.agents, candidate.id, &PersonalAgentDefinition::id);
        if (!latest.error.empty() || latest.collection_id != collection_id
            || (expected && (current == latest.agents.end() || *current != *expected))
            || (!expected && current != latest.agents.end()))
            throw std::runtime_error("Collection changed before publication; reload and retry.");
        personal_write_atomic(directory / "agent.toml", text.str());
        if (!expected)
            std::filesystem::rename(staging, destination);
    }
    catch (...)
    {
        std::error_code ignored;
        if (!expected)
            std::filesystem::remove_all(staging, ignored);
        throw;
    }
    return candidate;
}
}

namespace draxul
{
std::string personal_bootstrap_prompt(const std::filesystem::path& root, std::string_view id)
{
    if (!root.is_absolute() || !valid_personal_agent_id(id))
        throw std::runtime_error("Personal agent backing location is unavailable.");
    const auto folder = root / "agents" / std::string(id);
    return "You are a personal agent in Draxul. This is your persistent conversation.\n"
        "Your shared backing folder is " + utf8_path(folder) + ".\n"
        "Before responding, read instructions.md in that folder and any existing state.md. "
        "Keep durable working data in data/. After meaningful work, update state.md with decisions, "
        "current progress and next steps so you can resume. "
        "If instructions.md, state.md or data/ are missing, initialize only those missing items "
        "inside your existing backing folder; preserve existing files. If the Dropbox collection "
        "or your backing folder is unavailable, report that instead of creating another collection. "
        "The pill name is only a display label; agent.toml is Draxul metadata, so leave it alone. "
        "Follow the user's messages in this chat. When asked to change standing instructions, "
        "update instructions.md. When the user requests scheduled work, save the task, recurrence, timezone "
        "and next due time in schedule.md, and record completions in state.md. Draxul checks every five "
        "minutes while its server is running; checks may be delayed while busy or asleep. Do not claim "
        "exact-time delivery or create another scheduler. Do not invent a task or schedule. "
        "If no user task or background check was supplied, briefly acknowledge readiness and wait.\n";
}
}

namespace draxul
{
void delete_personal_agent(const std::filesystem::path& configured_root,std::string_view collection_id,
    const PersonalAgentDefinition& expected)
{
    if (!valid_personal_agent_id(expected.id)) throw std::runtime_error("Invalid agent identity.");
    const auto root=std::filesystem::canonical(configured_root);
    const auto snapshot=load_personal_agents(root);
    const auto found=std::ranges::find(snapshot.agents,expected.id,&PersonalAgentDefinition::id);
    if (!snapshot.error.empty() || snapshot.collection_id!=collection_id || found==snapshot.agents.end()
        || !found->error.empty() || *found!=expected)
        throw std::runtime_error("Agent changed or is unavailable. Review it before deleting.");
    const auto source=root/"agents"/expected.id;
    const auto trash=root/".deleted";
    std::filesystem::create_directories(trash);
    require_contained(root,trash);
    require_contained(root,source);
    if (std::filesystem::exists(trash/expected.id)) throw std::runtime_error("A deleted folder already uses this identity.");
    std::filesystem::rename(source,trash/expected.id);
}
}

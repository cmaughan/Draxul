#include <draxul/config_document.h>
#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <draxul/runtime_path.h>
#include <draxul/toml_support.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace draxul
{

namespace
{

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

std::filesystem::path config_temporary_path(const std::filesystem::path& path)
{
    static std::atomic<uint64_t> sequence = 0;
#ifdef _WIN32
    const auto process_id = static_cast<uint64_t>(GetCurrentProcessId());
#else
    const auto process_id = static_cast<uint64_t>(::getpid());
#endif
    std::filesystem::path temporary = path;
    temporary += ".tmp-" + std::to_string(process_id) + "-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
        + "-" + std::to_string(sequence++);
    return temporary;
}

} // namespace

Result<void, Error> write_config_toml_atomically(
    const std::filesystem::path& path, std::string_view content)
{
    std::filesystem::path temporary;
    std::filesystem::path destination = path;
    const auto fail = [&](const std::string& reason) {
        if (!temporary.empty())
        {
            std::error_code cleanup_error;
            std::filesystem::remove(temporary, cleanup_error);
        }
        return Result<void, Error>::err(Error::io(
            "Failed to save config to " + path.string() + ": " + reason));
    };

    try
    {
        std::error_code status_error;
        const auto status = std::filesystem::symlink_status(path, status_error);
        if (status_error && status_error != std::errc::no_such_file_or_directory)
            return fail(status_error.message());
        if (!status_error && std::filesystem::is_symlink(status))
            destination = std::filesystem::canonical(path);

        if (!destination.parent_path().empty())
            std::filesystem::create_directories(destination.parent_path());
        temporary = config_temporary_path(destination);
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out)
            return fail("could not open temporary file");
#ifndef _WIN32
        std::error_code permissions_error;
        const auto existing_status = std::filesystem::status(destination, permissions_error);
        if (permissions_error
            && permissions_error != std::errc::no_such_file_or_directory)
        {
            out.close();
            return fail("could not inspect file permissions: " + permissions_error.message());
        }
        const auto permissions = permissions_error
            ? std::filesystem::perms::owner_read | std::filesystem::perms::owner_write
            : existing_status.permissions();
        permissions_error.clear();
        std::filesystem::permissions(temporary, permissions,
            std::filesystem::perm_options::replace, permissions_error);
        if (permissions_error)
        {
            out.close();
            return fail("could not preserve file permissions: " + permissions_error.message());
        }
#endif
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        out.close();
        if (!out)
            return fail("could not write temporary file");

#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), destination.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            return fail(std::system_category().message(
                static_cast<int>(GetLastError())));
        }
#else
        std::error_code replace_error;
        std::filesystem::rename(temporary, destination, replace_error);
        if (replace_error)
            return fail(replace_error.message());
#endif
        return Result<void, Error>::ok();
    }
    catch (const std::exception& ex)
    {
        return fail(ex.what());
    }
}

std::filesystem::path ConfigDocument::default_path()
{
    PERF_MEASURE();
    return user_config_dir() / "draxul" / "config.toml";
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
    std::ostringstream serialized;
    serialized << document_ << '\n';
    auto saved = write_config_toml_atomically(path, serialized.str());
    if (!saved)
        DRAXUL_LOG_WARN(LogCategory::App, "%s", saved.error().message.c_str());
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

} // namespace draxul

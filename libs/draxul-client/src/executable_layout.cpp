#include <draxul/executable_layout.h>
#include <draxul/runtime_path.h>
#include <cwchar>

namespace draxul
{

std::filesystem::path resolve_client_executable(
    const std::vector<std::string>& args)
{
    std::error_code path_error;
    if (!args.empty() && !args.front().empty())
    {
        auto path = std::filesystem::absolute(args.front(), path_error);
        if (!path_error && std::filesystem::exists(path))
            return path;
    }
#ifdef _WIN32
    constexpr std::string_view executable_name = "draxul.exe";
#else
    constexpr std::string_view executable_name = "draxul";
#endif
    return executable_directory() / executable_name;
}

std::filesystem::path macos_server_helper_executable(
    const std::filesystem::path& client_executable)
{
    if (client_executable.filename() == "draxul-server")
        return client_executable;
    const auto macos_directory = client_executable.parent_path();
    const auto contents_directory = macos_directory.parent_path();
    if (macos_directory.filename() != "MacOS"
        || contents_directory.filename() != "Contents")
    {
        return client_executable;
    }
    return contents_directory / "Helpers" / "Draxul Server.app"
        / "Contents" / "MacOS" / "draxul-server";
}

std::filesystem::path macos_client_executable(
    const std::filesystem::path& current_executable)
{
    if (current_executable.filename() != "draxul-server")
        return current_executable;
    const auto helper_macos_directory
        = current_executable.parent_path();
    const auto helper_contents_directory
        = helper_macos_directory.parent_path();
    const auto helper_bundle_directory
        = helper_contents_directory.parent_path();
    const auto helpers_directory
        = helper_bundle_directory.parent_path();
    const auto client_contents_directory
        = helpers_directory.parent_path();
    return client_contents_directory / "MacOS" / "draxul";
}

#ifdef _WIN32
std::filesystem::path windows_server_helper_executable(
    const std::filesystem::path& client_executable)
{
    if (_wcsicmp(client_executable.filename().c_str(),
            L"draxul-server.exe")
        == 0)
    {
        return client_executable;
    }
    return client_executable.parent_path()
        / "draxul-server.exe";
}

std::filesystem::path windows_client_executable(
    const std::filesystem::path& current_executable)
{
    if (_wcsicmp(current_executable.filename().c_str(),
            L"draxul-server.exe")
        != 0)
    {
        return current_executable;
    }
    return current_executable.parent_path() / "draxul.exe";
}
#endif

std::filesystem::path default_server_log_path(
    const std::filesystem::path& runtime_directory)
{
    return runtime_directory / "draxul-server.log";
}

} // namespace draxul

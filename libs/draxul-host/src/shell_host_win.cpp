#include "conpty_process.h"
#include <draxul/local_terminal_host.h>

#include <draxul/host_kind.h>
#include <draxul/log.h>
#include <draxul/perf_timing.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdlib>
#include <filesystem>
#include <vector>

namespace draxul
{

namespace
{

// Probe candidate paths and return the first that exists on disk, or fallback.
std::string resolve_command(std::initializer_list<const char*> candidates, std::string fallback)
{
    for (const char* candidate : candidates)
    {
        if (std::filesystem::exists(candidate))
            return candidate;
    }
    return fallback;
}

// Find Git Bash at well-known installation paths, avoiding C:\Windows\System32\bash.exe (WSL).
std::string find_git_bash()
{
    // Check PROGRAMFILES and PROGRAMFILES(X86) env vars for portability.
    auto try_env = [](const char* env_var, const char* suffix) -> std::string {
        const char* base = std::getenv(env_var);
        if (!base)
            return {};
        std::filesystem::path p = std::filesystem::path(base) / suffix;
        if (std::filesystem::exists(p))
            return p.string();
        return {};
    };

    for (auto [env, suffix] : {
             std::pair{ "PROGRAMFILES", "Git\\bin\\bash.exe" },
             std::pair{ "PROGRAMFILES", "Git\\usr\\bin\\bash.exe" },
             std::pair{ "PROGRAMFILES(X86)", "Git\\bin\\bash.exe" },
             std::pair{ "PROGRAMFILES(X86)", "Git\\usr\\bin\\bash.exe" },
         })
    {
        auto found = try_env(env, suffix);
        if (!found.empty())
            return found;
    }

    // Last resort: bare name on PATH (may hit WSL bash, but user has no Git Bash installed).
    return "bash.exe";
}

std::string try_env_suffix(const char* env_var, const char* suffix)
{
    const char* base = std::getenv(env_var);
    if (!base)
        return {};
    std::filesystem::path path = std::filesystem::path(base) / suffix;
    if (std::filesystem::exists(path))
        return path.string();
    return {};
}

std::wstring widen_utf8(std::string_view text)
{
    if (text.empty())
        return {};
    const int size = MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0)
        return {};
    std::wstring wide(static_cast<size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size) <= 0)
        return {};
    return wide;
}

std::string narrow_utf8(std::wstring_view text)
{
    if (text.empty())
        return {};
    const int size = WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return {};
    std::string utf8(static_cast<size_t>(size), '\0');
    if (WideCharToMultiByte(
            CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), size, nullptr, nullptr)
        <= 0)
        return {};
    return utf8;
}

bool path_looks_like_windows_apps(std::wstring_view path)
{
    return path.find(L"\\WindowsApps\\") != std::wstring_view::npos
        || path.find(L"/WindowsApps/") != std::wstring_view::npos;
}

std::string try_local_app_exec_alias(std::string_view package_family_name, std::string_view exe_name)
{
    const char* local_app_data = std::getenv("LOCALAPPDATA");
    if (!local_app_data)
        return {};

    const std::filesystem::path path = std::filesystem::path(local_app_data)
        / "Microsoft" / "WindowsApps" / std::string(package_family_name) / std::string(exe_name);
    if (std::filesystem::exists(path))
        return path.string();
    return {};
}

std::string search_path_executable(std::string_view command)
{
    const std::wstring requested = widen_utf8(command);
    if (requested.empty())
        return {};

    DWORD required = SearchPathW(nullptr, requested.c_str(), nullptr, 0, nullptr, nullptr);
    if (required == 0)
        return {};

    std::wstring resolved(static_cast<size_t>(required), L'\0');
    required = SearchPathW(nullptr, requested.c_str(), nullptr,
        static_cast<DWORD>(resolved.size()), resolved.data(), nullptr);
    if (required == 0)
        return {};
    if (!resolved.empty() && resolved.back() == L'\0')
        resolved.pop_back();
    return narrow_utf8(resolved);
}

std::string find_pwsh()
{
    for (auto [env, suffix] : {
             std::pair{ "PROGRAMFILES", "PowerShell\\7\\pwsh.exe" },
             std::pair{ "PROGRAMFILES", "PowerShell\\7-preview\\pwsh.exe" },
             std::pair{ "PROGRAMFILES", "PowerShell\\6\\pwsh.exe" },
             std::pair{ "PROGRAMFILES(X86)", "PowerShell\\7\\pwsh.exe" },
             std::pair{ "PROGRAMFILES(X86)", "PowerShell\\7-preview\\pwsh.exe" },
             std::pair{ "PROGRAMFILES(X86)", "PowerShell\\6\\pwsh.exe" },
         })
    {
        if (auto found = try_env_suffix(env, suffix); !found.empty())
            return found;
    }

    const std::string path_hit = search_path_executable("pwsh.exe");
    const std::wstring path_hit_w = widen_utf8(path_hit);
    if (!path_hit.empty())
    {
        if (path_looks_like_windows_apps(path_hit_w))
            return "pwsh.exe";
        return path_hit;
    }

    if (!try_local_app_exec_alias("Microsoft.PowerShell_8wekyb3d8bbwe", "pwsh.exe").empty()
        || !try_local_app_exec_alias("Microsoft.PowerShellPreview_8wekyb3d8bbwe", "pwsh.exe").empty())
    {
        return "pwsh.exe";
    }

    return {};
}

std::string find_windows_powershell()
{
    wchar_t system_dir[MAX_PATH] = {};
    const UINT length = GetSystemDirectoryW(system_dir, MAX_PATH);
    if (length > 0 && length < MAX_PATH)
    {
        std::filesystem::path path = std::filesystem::path(system_dir) / "WindowsPowerShell" / "v1.0" / "powershell.exe";
        if (std::filesystem::exists(path))
            return path.string();
    }

    return "powershell.exe";
}

void append_unique(std::vector<std::string>& values, std::string value)
{
    if (value.empty())
        return;
    if (std::find(values.begin(), values.end(), value) == values.end())
        values.push_back(std::move(value));
}

class ConPtyHostBase : public LocalTerminalHost
{
protected:
    bool spawn_process(const std::string& command, const std::string& hint)
    {
        PERF_MEASURE();
        const std::string host_name_str(host_name());
        DRAXUL_LOG_DEBUG(LogCategory::App,
            "Starting %s host via ConPTY: command='%s' cwd='%s'",
            host_name_str.c_str(),
            command.c_str(),
            launch_options().working_dir.c_str());
        if (!process_.spawn(command, launch_options().args, launch_options().working_dir, grid_cols(), grid_rows(), [this]() {
                callbacks().wake_window();
            }))
        {
            set_init_error("Could not start " + command + ". " + hint);
            return false;
        }
        for (const auto& cmd : launch_options().startup_commands)
            process_.write(cmd + "\r");
        update_cursor_style();
        return true;
    }

    bool do_process_write(std::string_view text) override
    {
        return process_.write(text);
    }
    std::vector<std::string> do_process_drain() override
    {
        return process_.drain_output();
    }
    bool do_process_resize(int cols, int rows) override
    {
        return process_.resize(cols, rows);
    }
    bool do_process_is_running() const override
    {
        return process_.is_running();
    }
    std::optional<int> do_process_exit_code() const override
    {
        return process_.exit_code();
    }
    void do_process_shutdown() override
    {
        process_.shutdown();
    }
    void do_process_request_close() override
    {
        process_.request_close();
    }

    void init_colors()
    {
        PERF_MEASURE();
        highlights().set_default_fg(
            launch_options().terminal_fg.value_or(Color(0.92f, 0.92f, 0.92f, 1.0f)));
        highlights().set_default_bg(
            launch_options().terminal_bg.value_or(Color(0.08f, 0.09f, 0.10f, 1.0f)));
        apply_grid_size(viewport().grid_size.x, viewport().grid_size.y);
        reset_terminal_state();
    }

    ConPtyProcess& process()
    {
        return process_;
    }

private:
    ConPtyProcess process_;
};

class WinShellHost : public ConPtyHostBase
{
public:
    std::string_view host_name() const override
    {
        return launch_options().kind == HostKind::Zsh ? "zsh" : "bash";
    }

protected:
    bool initialize_host() override
    {
        PERF_MEASURE();
        init_colors();

        std::string command = launch_options().command;
        if (command.empty())
        {
            if (launch_options().kind == HostKind::Zsh)
                command = resolve_command({ "C:\\msys64\\usr\\bin\\zsh.exe" }, "zsh.exe");
            else
                command = find_git_bash();
        }

        return spawn_process(command,
            "Please ensure Git for Windows or MSYS2 is installed and the shell is on your PATH.");
    }
};

class PowerShellHost : public ConPtyHostBase
{
public:
    std::string_view host_name() const override
    {
        return "powershell";
    }

protected:
    bool initialize_host() override
    {
        PERF_MEASURE();
        init_colors();

        std::vector<std::string> args = launch_options().args;
        if (args.empty())
        {
            args = {
                "-NoLogo",
            };
        }

        const int cols = grid_cols();
        const int rows = grid_rows();
        std::vector<std::string> candidates;
        if (!launch_options().command.empty())
        {
            append_unique(candidates, launch_options().command);
        }
        else
        {
            append_unique(candidates, find_pwsh());
            append_unique(candidates, find_windows_powershell());
        }

        std::string command;
        for (const auto& candidate : candidates)
        {
            if (process().spawn(candidate, args, launch_options().working_dir, cols, rows, [this]() {
                    callbacks().wake_window();
                }))
            {
                command = candidate;
                break;
            }

            if (launch_options().command.empty())
            {
                DRAXUL_LOG_WARN(LogCategory::App,
                    "PowerShell candidate failed under ConPTY: '%s'", candidate.c_str());
            }
        }
        if (command.empty())
        {
            set_init_error("Could not start PowerShell. Please install PowerShell 7 or ensure powershell.exe is available.");
            return false;
        }

        for (const auto& startup : launch_options().startup_commands)
            process().write(startup + "\r");

        update_cursor_style();
        return true;
    }
};

class WslHost : public ConPtyHostBase
{
public:
    std::string_view host_name() const override
    {
        return "wsl";
    }

protected:
    bool initialize_host() override
    {
        PERF_MEASURE();
        init_colors();

        std::string command = launch_options().command;
        if (command.empty())
            command = "wsl.exe";

        return spawn_process(command,
            "Please ensure WSL is installed. Run 'wsl --install' from an elevated prompt.");
    }
};

} // namespace

std::unique_ptr<IHost> create_shell_host()
{
    PERF_MEASURE();
    return std::make_unique<WinShellHost>();
}

std::unique_ptr<IHost> create_powershell_host()
{
    PERF_MEASURE();
    return std::make_unique<PowerShellHost>();
}

std::unique_ptr<IHost> create_wsl_host()
{
    PERF_MEASURE();
    return std::make_unique<WslHost>();
}

} // namespace draxul

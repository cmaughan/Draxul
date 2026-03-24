#include "conpty_process.h"
#include <draxul/local_terminal_host.h>

#include <draxul/host_kind.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <filesystem>

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

class ConPtyHostBase : public LocalTerminalHost
{
protected:
    bool spawn_process(const std::string& command, const std::string& hint)
    {
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

class ShellHost : public ConPtyHostBase
{
public:
    std::string_view host_name() const override
    {
        return launch_options().kind == HostKind::Zsh ? "zsh" : "bash";
    }

protected:
    bool initialize_host() override
    {
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
        init_colors();

        std::string command = launch_options().command.empty() ? "pwsh.exe" : launch_options().command;
        std::vector<std::string> args = launch_options().args;
        if (args.empty())
        {
            args = {
                "-NoLogo",
                "-NoExit",
                "-Command",
                "[Console]::InputEncoding=[System.Text.UTF8Encoding]::UTF8; [Console]::OutputEncoding=[System.Text.UTF8Encoding]::UTF8"
            };
        }

        const int cols = grid_cols();
        const int rows = grid_rows();
        if (!process().spawn(command, args, launch_options().working_dir, cols, rows, [this]() {
                callbacks().wake_window();
            }))
        {
            if (launch_options().command.empty() && command != "powershell.exe"
                && process().spawn("powershell.exe", args, launch_options().working_dir, cols, rows, [this]() {
                       callbacks().wake_window();
                   }))
            {
                command = "powershell.exe";
            }
            else
            {
                set_init_error("Could not start PowerShell. Please install PowerShell 7 or ensure powershell.exe is available.");
                return false;
            }
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
    return std::make_unique<ShellHost>();
}

std::unique_ptr<IHost> create_powershell_host()
{
    return std::make_unique<PowerShellHost>();
}

std::unique_ptr<IHost> create_wsl_host()
{
    return std::make_unique<WslHost>();
}

} // namespace draxul

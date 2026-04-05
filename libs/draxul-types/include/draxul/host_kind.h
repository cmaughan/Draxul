#pragma once

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace draxul
{

enum class HostKind
{
    Nvim,
    PowerShell,
    Bash,
    Zsh,
    Wsl,
    MegaCity,
    NanoVGDemo,
};

inline std::optional<HostKind> parse_host_kind(std::string_view value)
{
    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (normalized == "nvim")
        return HostKind::Nvim;
    if (normalized == "powershell" || normalized == "pwsh")
        return HostKind::PowerShell;
    if (normalized == "bash")
        return HostKind::Bash;
    if (normalized == "zsh")
        return HostKind::Zsh;
    if (normalized == "wsl")
        return HostKind::Wsl;
    if (normalized == "megacity")
        return HostKind::MegaCity;
    if (normalized == "nanovg" || normalized == "nanovg-demo")
        return HostKind::NanoVGDemo;
    return std::nullopt;
}

inline const char* to_string(HostKind kind)
{
    switch (kind)
    {
    case HostKind::Nvim:
        return "nvim";
    case HostKind::PowerShell:
        return "powershell";
    case HostKind::Bash:
        return "bash";
    case HostKind::Zsh:
        return "zsh";
    case HostKind::Wsl:
        return "wsl";
    case HostKind::MegaCity:
        return "megacity";
    case HostKind::NanoVGDemo:
        return "nanovg-demo";
    }
    return "nvim";
}

} // namespace draxul

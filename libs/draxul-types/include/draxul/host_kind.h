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
    BioView,
    SatView,
    NanoVGDemo,
    Markdown,
    Kanban,
    Score,
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
    if (normalized == "bioview" || normalized == "bio")
        return HostKind::BioView;
    if (normalized == "satview" || normalized == "sat")
        return HostKind::SatView;
    if (normalized == "nanovg" || normalized == "nanovg-demo")
        return HostKind::NanoVGDemo;
    if (normalized == "markdown" || normalized == "md")
        return HostKind::Markdown;
    if (normalized == "kanban" || normalized == "kb")
        return HostKind::Kanban;
    if (normalized == "score" || normalized == "scoreview")
        return HostKind::Score;
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
    case HostKind::BioView:
        return "bioview";
    case HostKind::SatView:
        return "satview";
    case HostKind::NanoVGDemo:
        return "nanovg-demo";
    case HostKind::Markdown:
        return "markdown";
    case HostKind::Kanban:
        return "kanban";
    case HostKind::Score:
        return "score";
    }
    return "nvim";
}

} // namespace draxul

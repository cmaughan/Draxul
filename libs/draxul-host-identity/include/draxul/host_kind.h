#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <span>

namespace draxul
{

enum class HostKind
{
    Nvim,
    PowerShell,
    Bash,
    Zsh,
    Wsl,
    RemoteTerminal,
    MegaCity,
    BioView,
    SatView,
    NanoVGDemo,
    Markdown,
    Kanban,
    Score,
};

inline constexpr std::array kAllHostKinds = {
    HostKind::Nvim,
    HostKind::PowerShell,
    HostKind::Bash,
    HostKind::Zsh,
    HostKind::Wsl,
    HostKind::RemoteTerminal,
    HostKind::MegaCity,
    HostKind::BioView,
    HostKind::SatView,
    HostKind::NanoVGDemo,
    HostKind::Markdown,
    HostKind::Kanban,
    HostKind::Score,
};

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
    case HostKind::RemoteTerminal:
        return "remote-terminal";
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

inline std::span<const std::string_view> host_kind_aliases(HostKind kind)
{
    static constexpr std::array<std::string_view, 0> none = {};
    static constexpr std::array powershell = { std::string_view("pwsh") };
    static constexpr std::array remote_terminal = { std::string_view("remote") };
    static constexpr std::array bioview = { std::string_view("bio") };
    static constexpr std::array satview = { std::string_view("sat") };
    static constexpr std::array nanovg = { std::string_view("nanovg") };
    static constexpr std::array markdown = { std::string_view("md") };
    static constexpr std::array kanban = { std::string_view("kb") };
    static constexpr std::array score = { std::string_view("scoreview") };
    switch (kind)
    {
    case HostKind::PowerShell:
        return powershell;
    case HostKind::RemoteTerminal:
        return remote_terminal;
    case HostKind::BioView:
        return bioview;
    case HostKind::SatView:
        return satview;
    case HostKind::NanoVGDemo:
        return nanovg;
    case HostKind::Markdown:
        return markdown;
    case HostKind::Kanban:
        return kanban;
    case HostKind::Score:
        return score;
    default:
        return none;
    }
}

inline std::optional<HostKind> parse_host_kind(std::string_view value)
{
    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    for (const HostKind kind : kAllHostKinds)
    {
        if (normalized == to_string(kind))
            return kind;
        for (const auto alias : host_kind_aliases(kind))
        {
            if (normalized == alias)
                return kind;
        }
    }
    return std::nullopt;
}

} // namespace draxul

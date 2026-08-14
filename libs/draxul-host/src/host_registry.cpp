#include <draxul/host_registry.h>

#include <draxul/host.h>
#include <draxul/perf_timing.h>

#include <sstream>

namespace draxul
{

namespace
{

HostProviderMetadata default_metadata(HostKind kind)
{
    HostProviderMetadata metadata;
    metadata.kind = kind;
    metadata.canonical_cli_name = to_string(kind);
    for (const auto alias : host_kind_aliases(kind))
        metadata.aliases.emplace_back(alias);
    metadata.launch_contexts = HostLaunchContext::Cli | HostLaunchContext::Split | HostLaunchContext::NewTab;

    switch (kind)
    {
    case HostKind::Nvim:
        metadata.display_name = "Neovim";
        break;
    case HostKind::PowerShell:
        metadata.display_name = "PowerShell";
        metadata.platforms = { .windows = true, .macos = false, .other_unix = false };
        break;
    case HostKind::Bash:
        metadata.display_name = "Bash";
        break;
    case HostKind::Zsh:
        metadata.display_name = "Zsh";
        break;
    case HostKind::Wsl:
        metadata.display_name = "WSL";
        metadata.platforms = { .windows = true, .macos = false, .other_unix = false };
        break;
    case HostKind::RemoteTerminal:
        metadata.display_name = "Remote Terminal";
        metadata.palette_visible = false;
        metadata.launch_contexts = HostLaunchContext::None;
        break;
    case HostKind::NanoVGDemo:
        metadata.display_name = "NanoVG Demo";
        metadata.palette_visible = false;
        metadata.test_only = true;
        metadata.launch_contexts = HostLaunchContext::Cli;
        break;
    case HostKind::Markdown:
        metadata.display_name = "Markdown";
        metadata.palette_visible = false;
        metadata.launch_contexts = HostLaunchContext::Cli;
        break;
    case HostKind::Kanban:
        metadata.display_name = "Kanban";
        break;
    case HostKind::Plugin:
        metadata.display_name = "Plugin";
        metadata.palette_visible = false;
        metadata.launch_contexts = HostLaunchContext::None;
        break;
    case HostKind::Count:
        break;
    }
    return metadata;
}

} // namespace

bool host_provider_available_on_current_platform(
    const HostProviderMetadata::PlatformAvailability& availability)
{
#ifdef _WIN32
    return availability.windows;
#elif defined(__APPLE__)
    return availability.macos;
#else
    return availability.other_unix;
#endif
}

void HostProviderRegistry::register_provider(HostKind kind, HostFactory factory)
{
    register_provider(default_metadata(kind), std::move(factory));
}

void HostProviderRegistry::register_provider(HostProviderMetadata metadata, HostFactory factory)
{
    PERF_MEASURE();
    if (!factory || !host_provider_available_on_current_platform(metadata.platforms))
        return;
    for (auto& entry : providers_)
    {
        if (entry.metadata.kind == metadata.kind)
        {
            entry.metadata = std::move(metadata);
            entry.factory = std::move(factory);
            return;
        }
    }
    providers_.push_back(Entry{ std::move(metadata), std::move(factory) });
}

void HostProviderRegistry::register_metadata(HostKind kind)
{
    HostProviderMetadata metadata = default_metadata(kind);
    if (!host_provider_available_on_current_platform(
            metadata.platforms))
    {
        return;
    }
    for (auto& entry : providers_)
    {
        if (entry.metadata.kind == kind)
        {
            entry.metadata = std::move(metadata);
            return;
        }
    }
    providers_.push_back(
        Entry{ std::move(metadata), {} });
}

bool HostProviderRegistry::has(HostKind kind) const
{
    for (const auto& entry : providers_)
    {
        if (entry.metadata.kind == kind)
            return true;
    }
    return false;
}

std::unique_ptr<IHost> HostProviderRegistry::create(HostKind kind) const
{
    PERF_MEASURE();
    for (const auto& entry : providers_)
    {
        if (entry.metadata.kind == kind && entry.factory)
            return entry.factory();
    }
    return nullptr;
}

const HostProviderMetadata* HostProviderRegistry::metadata(HostKind kind) const
{
    for (const auto& entry : providers_)
    {
        if (entry.metadata.kind == kind)
            return &entry.metadata;
    }
    return nullptr;
}

std::vector<HostProviderMetadata> HostProviderRegistry::available_providers() const
{
    std::vector<HostProviderMetadata> result;
    result.reserve(providers_.size());
    for (const auto& entry : providers_)
        result.push_back(entry.metadata);
    return result;
}

std::optional<HostKind> HostProviderRegistry::parse_available_kind(std::string_view value) const
{
    const auto parsed = parse_host_kind(value);
    return parsed && has(*parsed) ? parsed : std::nullopt;
}

std::string HostProviderRegistry::available_cli_names() const
{
    std::ostringstream names;
    bool first = true;
    for (const auto& entry : providers_)
    {
        if (entry.metadata.test_only
            || !supports_launch_context(entry.metadata.launch_contexts, HostLaunchContext::Cli))
            continue;
        if (!first)
            names << ", ";
        names << entry.metadata.canonical_cli_name;
        first = false;
    }
    return names.str();
}

void HostProviderRegistry::clear()
{
    providers_.clear();
}

HostProviderRegistry& HostProviderRegistry::global()
{
    static HostProviderRegistry instance;
    return instance;
}

} // namespace draxul

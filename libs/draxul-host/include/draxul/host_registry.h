#pragma once

#include <draxul/host_kind.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// HostProviderRegistry — narrow plugin-style boundary for host kinds.
//
// Built-in hosts (nvim, shell variants, nanovg demo) are registered at startup
// via register_builtin_host_providers(). Optional modules — currently MegaCity
// — register themselves through the same interface from the executable, so the
// core libraries (draxul-host, draxul-app) carry no headers, types, or symbols
// from those modules. A build with an optional module disabled simply has no
// provider for its HostKind, and create() returns nullptr.
//
// Intentionally tiny: this is a source-level boundary, not an ABI. We can
// extend it later (metadata, capability flags, dynamic loading) once the
// product shape calls for it.

namespace draxul
{

class IHost;

using HostFactory = std::function<std::unique_ptr<IHost>()>;

enum class HostLaunchContext : uint8_t
{
    None = 0,
    Cli = 1 << 0,
    Split = 1 << 1,
    NewWorkspace = 1 << 2,
};

inline HostLaunchContext operator|(HostLaunchContext lhs, HostLaunchContext rhs)
{
    return static_cast<HostLaunchContext>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
}

inline bool supports_launch_context(HostLaunchContext available, HostLaunchContext requested)
{
    return (static_cast<uint8_t>(available) & static_cast<uint8_t>(requested)) != 0;
}

struct HostProviderMetadata
{
    struct PlatformAvailability
    {
        bool windows = true;
        bool macos = true;
        bool other_unix = true;
    };

    HostKind kind = HostKind::Nvim;
    std::string display_name;
    std::string canonical_cli_name;
    std::vector<std::string> aliases;
    bool palette_visible = true;
    bool test_only = false;
    PlatformAvailability platforms;
    HostLaunchContext launch_contexts = HostLaunchContext::None;
};

bool host_provider_available_on_current_platform(
    const HostProviderMetadata::PlatformAvailability& availability);

class HostProviderRegistry
{
public:
    void register_provider(HostKind kind, HostFactory factory);
    void register_provider(HostProviderMetadata metadata, HostFactory factory);
    bool has(HostKind kind) const;
    std::unique_ptr<IHost> create(HostKind kind) const;
    const HostProviderMetadata* metadata(HostKind kind) const;
    std::vector<HostProviderMetadata> available_providers() const;
    std::optional<HostKind> parse_available_kind(std::string_view value) const;
    std::string available_cli_names() const;
    void clear();

    static HostProviderRegistry& global();

private:
    struct Entry
    {
        HostProviderMetadata metadata;
        HostFactory factory;
    };
    std::vector<Entry> providers_;
};

// Registers the host kinds that ship with the core terminal product
// (Nvim, shell family, NanoVG demo). Optional modules are not registered here.
void register_builtin_host_providers(HostProviderRegistry& registry);

} // namespace draxul

#pragma once

#include <draxul/host_kind.h>
#include <draxul/session_model.h>
#include <draxul/topology_protocol.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace draxul
{

enum class TopologyMutationKind
{
    CreateSpace,
    RenameSpace,
    CloseSpace,
    CreateTab,
    RenameTab,
    CloseTab,
    MoveTab,
    SplitPane,
    DuplicatePane,
    ClosePane,
    RenamePane,
    SwapPane,
    RestartPane,
    SetSplitRatio,
    EqualizeSplits,
};

// Local identities are the common language between App and either mutation
// route. The server-backed route resolves them to durable protocol identities;
// the local route applies them directly to the controllers.
struct TopologyMutation
{
    TopologyMutationKind kind = TopologyMutationKind::SplitPane;
    SpaceId space_id = kInvalidSpaceId;
    int tab_id = -1;
    LeafId pane_id = kInvalidLeaf;
    LeafId target_pane_id = kInvalidLeaf;
    DividerId divider_id = kInvalidDivider;
    std::string name;
    std::filesystem::path root_directory;
    TopologySplitDirection direction
        = TopologySplitDirection::Vertical;
    std::optional<HostKind> host_kind;
    float ratio = 0.5f;
    float ratio_delta = 0.0f;
    int move_delta = 0;
    int pixel_width = 0;
    int pixel_height = 0;
};

enum class TopologyMutationState
{
    Applied,
    Enqueued,
    Rejected,
};

struct TopologyMutationResult
{
    TopologyMutationState state = TopologyMutationState::Rejected;
    SpaceId space_id = kInvalidSpaceId;
    int tab_id = -1;
    LeafId pane_id = kInvalidLeaf;
    std::string error;

    bool accepted() const noexcept
    {
        return state != TopologyMutationState::Rejected;
    }

    bool applied_locally() const noexcept
    {
        return state == TopologyMutationState::Applied;
    }

    static TopologyMutationResult applied();
    static TopologyMutationResult enqueued();
    static TopologyMutationResult rejected(std::string error);
};

enum class TopologyMutationRouteKind
{
    Local,
    ServerBacked,
};

class ITopologyMutationRoute
{
public:
    virtual ~ITopologyMutationRoute() = default;
    virtual TopologyMutationRouteKind route_kind() const noexcept = 0;
    virtual TopologyMutationResult mutate(
        const TopologyMutation& mutation) = 0;
};

// The local controllers are app-owned, so their implementation is supplied as
// one callback. This keeps the route interface testable without leaking
// PaneManager, SpaceController, or renderer dependencies into a shared library.
class LocalTopologyMutationRoute final
    : public ITopologyMutationRoute
{
public:
    using Handler = std::function<TopologyMutationResult(
        const TopologyMutation&)>;

    explicit LocalTopologyMutationRoute(Handler handler);
    TopologyMutationRouteKind route_kind() const noexcept override;
    TopologyMutationResult mutate(
        const TopologyMutation& mutation) override;

private:
    Handler handler_;
};

class ServerTopologyMutationRoute final
    : public ITopologyMutationRoute
{
public:
    struct Deps
    {
        std::function<std::optional<std::string>(SpaceId)>
            resolve_space;
        std::function<std::optional<std::string>(SpaceId, int)>
            resolve_tab;
        std::function<std::optional<std::string>(
            SpaceId, int, LeafId)>
            resolve_pane;
        std::function<std::optional<std::string>(
            std::string_view, DividerId)>
            resolve_divider;
        std::function<std::optional<TopologyPaneDomain>(
            SpaceId, int, LeafId)>
            resolve_pane_domain;
        std::function<bool(TopologyCommand, std::string&)>
            enqueue;
        std::function<TopologyMutationResult(
            const TopologyMutation&)>
            apply_client_local;
        HostKind platform_default_host_kind = HostKind::PowerShell;
    };

    explicit ServerTopologyMutationRoute(Deps deps);
    TopologyMutationRouteKind route_kind() const noexcept override;
    TopologyMutationResult mutate(
        const TopologyMutation& mutation) override;

private:
    TopologyMutationResult enqueue(
        TopologyCommand command) const;
    TopologyMutationResult reject_unresolved(
        std::string_view subject) const;

    Deps deps_;
};

std::unique_ptr<ITopologyMutationRoute>
make_topology_mutation_route(bool server_backed,
    LocalTopologyMutationRoute::Handler local_handler,
    ServerTopologyMutationRoute::Deps server_deps);

} // namespace draxul

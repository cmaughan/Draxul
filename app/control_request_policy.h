#pragma once

#include "agent_controller.h"
#include "space_id.h"

#include <draxul/agent_model.h>
#include <draxul/control_plane.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace draxul
{

// App-private request policy for the local UI control endpoint. This class owns
// request validation, limits, error/response mapping, and method dispatch. The
// callbacks retain App's subsystem ownership and ordered main-thread effects.
class ControlRequestPolicy
{
public:
    struct PaneTarget
    {
        std::string pane_id;
        SpaceId space_id = kInvalidSpaceId;
        int tab_id = -1;
        bool has_host = false;
    };

    enum class FocusResult
    {
        Focused,
        Failed,
    };

    enum class ActionResult
    {
        Dispatched,
        Rejected,
    };

    struct PluginReloadResult
    {
        std::string generation;
        int matched = 0;
        int reloaded = 0;
        bool rolled_back = false;
        std::string warning;
        std::string error;
    };

    enum class NativeRouteResult
    {
        Matched,
        Mismatch,
    };

    enum class NativeSessionResult
    {
        Accepted,
        Duplicate,
        Stale,
    };

    struct OperationResult
    {
        bool ok = false;
        std::string error;

        static OperationResult success()
        {
            return { true, {} };
        }

        static OperationResult failure(std::string message)
        {
            return { false, std::move(message) };
        }
    };

    struct LaunchResult
    {
        std::optional<std::string> instance_id;
        std::string error;
    };

    enum class AgentRouteResult
    {
        Ready,
        Missing,
        NoLiveHost,
    };

    struct Operations
    {
        // Preserves the existing read-only router as the response projection
        // boundary and as the final fallback for read/unknown methods.
        std::function<ControlMethodResult(const ControlRequest&)> read;

        std::function<std::optional<PaneTarget>(std::string_view)> find_pane;
        std::function<FocusResult(const PaneTarget&)> focus_pane;
        std::function<ActionResult(const PaneTarget&, std::string_view)> dispatch_pane_action;
        std::function<PluginReloadResult(std::string_view)> reload_plugin;

        std::function<NativeRouteResult(
            std::string_view pane_id,
            std::string_view instance_id,
            std::string_view agent_kind)>
            inspect_native_route;
        std::function<NativeSessionResult(
            std::string_view pane_id,
            std::string_view instance_id,
            AgentSessionRef session_ref)>
            accept_native_session;

        std::function<OperationResult(SpaceId)> focus_space;
        std::function<LaunchResult(AgentLaunchRequest)> launch_agent;
        std::function<std::optional<AgentProjection>(std::string_view)> find_agent;
        std::function<OperationResult(std::string_view)> focus_agent;
        std::function<OperationResult(const AgentProjection&)> restart_agent;
        std::function<AgentRouteResult(const AgentProjection&)> inspect_agent_route;
        std::function<bool(const AgentProjection&, std::string_view)> send_agent_input;

        std::function<nlohmann::json(uint64_t cursor, size_t limit)> read_events;
    };

    explicit ControlRequestPolicy(Operations operations);

    ControlMethodResult handle(const ControlRequest& request) const;

private:
    ControlMethodResult read_agent(
        const ControlRequest& request, std::string_view instance_id) const;

    Operations operations_;
};

} // namespace draxul

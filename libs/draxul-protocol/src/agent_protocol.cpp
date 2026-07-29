#include <draxul/agent_protocol.h>

#include <nlohmann/json.hpp>
#include <unordered_set>
#include <utility>

namespace draxul
{

namespace
{

bool valid_text(const std::string& value, bool allow_empty = false)
{
    return value.size() <= kServerAgentMaxTextBytes
        && (allow_empty || !value.empty());
}

bool read_text(const nlohmann::json& object, const char* key,
    std::string& value, bool allow_empty = false)
{
    const auto found = object.find(key);
    if (found == object.end() || !found->is_string())
        return false;
    value = found->get<std::string>();
    return valid_text(value, allow_empty);
}

std::optional<AgentIdentityOrigin> parse_identity_origin(
    std::string_view value)
{
    if (value == "managed")
        return AgentIdentityOrigin::Managed;
    if (value == "discovered")
        return AgentIdentityOrigin::Discovered;
    return std::nullopt;
}

std::optional<AgentLifecycle> parse_lifecycle(std::string_view value)
{
    if (value == "starting")
        return AgentLifecycle::Starting;
    if (value == "running")
        return AgentLifecycle::Running;
    if (value == "exited")
        return AgentLifecycle::Exited;
    if (value == "failed")
        return AgentLifecycle::Failed;
    return std::nullopt;
}

std::optional<AgentStatus> parse_status(std::string_view value)
{
    if (value == "unknown")
        return AgentStatus::Unknown;
    if (value == "idle")
        return AgentStatus::Idle;
    if (value == "working")
        return AgentStatus::Working;
    if (value == "blocked")
        return AgentStatus::Blocked;
    if (value == "done")
        return AgentStatus::Done;
    return std::nullopt;
}

std::optional<AgentStateAuthority> parse_authority(
    std::string_view value)
{
    if (value == "none")
        return AgentStateAuthority::None;
    if (value == "direct_host")
        return AgentStateAuthority::DirectHost;
    if (value == "screen_manifest")
        return AgentStateAuthority::ScreenManifest;
    if (value == "official_integration")
        return AgentStateAuthority::OfficialIntegration;
    return std::nullopt;
}

nlohmann::json identity_to_json(const AgentIdentity& identity)
{
    return {
        { "profile_id", identity.profile_id },
        { "kind", identity.kind },
        { "display_name", identity.display_name },
        { "instance_id", identity.instance_id },
        { "origin", to_string(identity.origin) },
    };
}

bool read_identity(
    const nlohmann::json& value, AgentIdentity& identity)
{
    if (!value.is_object()
        || !read_text(value, "profile_id", identity.profile_id, true)
        || !read_text(value, "kind", identity.kind)
        || !read_text(value, "display_name", identity.display_name)
        || !read_text(value, "instance_id", identity.instance_id))
    {
        return false;
    }
    std::string origin;
    if (!read_text(value, "origin", origin))
        return false;
    const auto parsed = parse_identity_origin(origin);
    if (!parsed)
        return false;
    identity.origin = *parsed;
    return true;
}

nlohmann::json session_ref_to_json(const AgentSessionRef& ref)
{
    return {
        { "source", ref.source },
        { "agent_kind", ref.agent_kind },
        { "integration_version", ref.integration_version },
        { "sequence", ref.sequence },
        { "kind", to_string(ref.kind) },
        { "value", ref.value },
    };
}

bool read_session_ref(
    const nlohmann::json& value, AgentSessionRef& ref)
{
    if (!value.is_object()
        || !read_text(value, "source", ref.source)
        || !read_text(value, "agent_kind", ref.agent_kind)
        || !read_text(value, "value", ref.value)
        || !value.contains("integration_version")
        || !value["integration_version"].is_number_unsigned()
        || !value.contains("sequence")
        || !value["sequence"].is_number_unsigned())
    {
        return false;
    }
    std::string kind;
    if (!read_text(value, "kind", kind))
        return false;
    const auto parsed_kind = parse_agent_session_ref_kind(kind);
    if (!parsed_kind)
        return false;
    ref.integration_version
        = value["integration_version"].get<uint32_t>();
    ref.sequence = value["sequence"].get<uint64_t>();
    ref.kind = *parsed_kind;
    return validate_agent_session_ref(ref);
}

nlohmann::json projection_to_json(
    const ServerAgentProjection& projection)
{
    nlohmann::json value{
        { "space_id", projection.space_id },
        { "tab_id", projection.tab_id },
        { "pane_id", projection.pane_id },
        { "terminal_id", projection.terminal_id },
        { "identity", identity_to_json(projection.identity) },
        { "identity_evidence_category",
            projection.identity_evidence_category },
        { "identity_high_confidence",
            projection.identity_high_confidence },
        { "lifecycle", to_string(projection.lifecycle) },
        { "generation", projection.generation.value },
        { "status", to_string(projection.status) },
        { "status_authority",
            to_string(projection.status_authority) },
        { "manifest_id", projection.manifest_id },
        { "manifest_version", projection.manifest_version },
        { "rule_id", projection.rule_id },
        { "status_evidence_category",
            projection.status_evidence_category },
        { "fallback_reason", projection.fallback_reason },
        { "observation_generation",
            projection.observation_generation },
        { "attention", projection.attention },
        { "running", projection.running },
    };
    if (projection.session_ref)
    {
        value["session_ref"]
            = session_ref_to_json(*projection.session_ref);
    }
    if (projection.exit_code)
        value["exit_code"] = *projection.exit_code;
    return value;
}

bool read_projection(const nlohmann::json& value,
    ServerAgentProjection& projection)
{
    if (!value.is_object()
        || !read_text(value, "space_id", projection.space_id)
        || !read_text(value, "tab_id", projection.tab_id)
        || !read_text(value, "pane_id", projection.pane_id)
        || !read_text(value, "terminal_id", projection.terminal_id)
        || !value.contains("identity")
        || !read_identity(value["identity"], projection.identity)
        || !read_text(value, "identity_evidence_category",
            projection.identity_evidence_category)
        || !value.contains("identity_high_confidence")
        || !value["identity_high_confidence"].is_boolean()
        || !value.contains("generation")
        || !value["generation"].is_number_unsigned()
        || !value.contains("manifest_version")
        || !value["manifest_version"].is_number_unsigned()
        || !value.contains("observation_generation")
        || !value["observation_generation"].is_number_unsigned()
        || !value.contains("attention")
        || !value["attention"].is_boolean()
        || !value.contains("running")
        || !value["running"].is_boolean())
    {
        return false;
    }
    std::string lifecycle;
    std::string status;
    std::string authority;
    if (!read_text(value, "lifecycle", lifecycle)
        || !read_text(value, "status", status)
        || !read_text(value, "status_authority", authority)
        || !read_text(value, "manifest_id",
            projection.manifest_id, true)
        || !read_text(value, "rule_id", projection.rule_id, true)
        || !read_text(value, "status_evidence_category",
            projection.status_evidence_category, true)
        || !read_text(value, "fallback_reason",
            projection.fallback_reason, true))
    {
        return false;
    }
    const auto parsed_lifecycle = parse_lifecycle(lifecycle);
    const auto parsed_status = parse_status(status);
    const auto parsed_authority = parse_authority(authority);
    if (!parsed_lifecycle || !parsed_status || !parsed_authority)
        return false;
    projection.identity_high_confidence
        = value["identity_high_confidence"].get<bool>();
    projection.lifecycle = *parsed_lifecycle;
    projection.generation.value = value["generation"].get<uint64_t>();
    projection.status = *parsed_status;
    projection.status_authority = *parsed_authority;
    projection.manifest_version
        = value["manifest_version"].get<uint32_t>();
    projection.observation_generation
        = value["observation_generation"].get<uint64_t>();
    projection.attention = value["attention"].get<bool>();
    projection.running = value["running"].get<bool>();
    if (const auto exit = value.find("exit_code"); exit != value.end())
    {
        if (!exit->is_number_integer())
            return false;
        projection.exit_code = exit->get<int>();
    }
    if (const auto session = value.find("session_ref");
        session != value.end())
    {
        AgentSessionRef ref;
        if (!read_session_ref(*session, ref))
            return false;
        projection.session_ref = std::move(ref);
    }
    return true;
}

} // namespace

nlohmann::json server_agent_snapshot_to_json(
    const ServerAgentSnapshot& snapshot)
{
    nlohmann::json agents = nlohmann::json::array();
    for (const auto& projection : snapshot.agents)
        agents.push_back(projection_to_json(projection));
    return {
        { "revision", snapshot.revision },
        { "session_id", snapshot.session_id },
        { "agents", std::move(agents) },
    };
}

std::optional<ServerAgentSnapshot> server_agent_snapshot_from_json(
    const nlohmann::json& value, std::string& error)
{
    if (!value.is_object()
        || !value.contains("revision")
        || !value["revision"].is_number_unsigned()
        || !value.contains("agents")
        || !value["agents"].is_array())
    {
        error = "Invalid server agent snapshot envelope.";
        return std::nullopt;
    }
    ServerAgentSnapshot snapshot;
    if (!read_text(value, "session_id", snapshot.session_id))
    {
        error = "Invalid server agent Session identity.";
        return std::nullopt;
    }
    if (value["agents"].size() > kServerAgentMaxRows)
    {
        error = "Server agent snapshot exceeds the row limit.";
        return std::nullopt;
    }
    snapshot.revision = value["revision"].get<uint64_t>();
    std::unordered_set<std::string> instance_ids;
    for (const auto& encoded : value["agents"])
    {
        ServerAgentProjection projection;
        if (!read_projection(encoded, projection)
            || !instance_ids.insert(
                    projection.identity.instance_id)
                    .second)
        {
            error = "Invalid or duplicate server agent projection.";
            return std::nullopt;
        }
        snapshot.agents.push_back(std::move(projection));
    }
    error.clear();
    return snapshot;
}

} // namespace draxul

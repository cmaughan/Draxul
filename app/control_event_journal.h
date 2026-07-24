#pragma once

#include "agent_controller.h"

#include <cstdint>
#include <deque>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace draxul
{

// Bounded cursor journal for local control clients. It retains sanitized
// state transitions only; terminal observations never enter the journal.
class ControlEventJournal
{
public:
    void observe(SpaceId active_space_id, const std::vector<AgentProjection>& agents);
    void record(std::string type, nlohmann::json payload);
    nlohmann::json read_after(uint64_t cursor, size_t limit) const;

private:
    struct AgentState
    {
        AgentRuntimeGeneration generation{};
        AgentLifecycle lifecycle = AgentLifecycle::Starting;
        AgentStatus status = AgentStatus::Unknown;
        bool focused = false;

        bool operator==(const AgentState&) const = default;
    };

    uint64_t next_sequence_ = 1;
    SpaceId observed_active_space_ = kInvalidSpaceId;
    bool observed_once_ = false;
    std::unordered_map<std::string, AgentState> agent_states_;
    std::deque<nlohmann::json> events_;
};

} // namespace draxul

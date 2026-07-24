#pragma once

#include <string>

namespace draxul
{

// Durable metadata for an agent intentionally launched into a pane. Process
// state is deliberately absent: runtime state is projected from the live host.
struct AgentIdentity
{
    std::string kind;
    std::string display_name;
    std::string instance_id;

    bool operator==(const AgentIdentity&) const = default;
};

} // namespace draxul

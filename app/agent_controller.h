#pragma once

#include "space_id.h"
#include "split_tree.h"
#include <draxul/agent_model.h>

#include <string>
#include <string_view>
#include <vector>

namespace draxul
{

class SpaceController;

struct AgentProjection
{
    SpaceId space_id = kInvalidSpaceId;
    int tab_id = -1;
    LeafId leaf_id = kInvalidLeaf;
    std::string pane_id;
    AgentIdentity identity;
    bool running = false;
    bool focused = false;
};

// Derives the Agents index from pane-owned identities and resolves UI
// navigation back to the owning Space, tab, and pane.
class AgentController
{
public:
    std::vector<AgentProjection> query(const SpaceController& spaces) const;
    bool focus(SpaceController& spaces, std::string_view instance_id) const;
    bool focus_by_index(SpaceController& spaces, int one_based_index) const;
};

} // namespace draxul

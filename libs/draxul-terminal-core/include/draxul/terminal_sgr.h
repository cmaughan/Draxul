#pragma once

#include <draxul/grid.h>

#include <vector>

namespace draxul
{

// Free function that applies a parsed SGR (Select Graphic Rendition) parameter
// list to a highlight attribute. Called from TerminalCore::csi_sgr().
void apply_sgr(HlAttr& attr, const std::vector<int>& params);

} // namespace draxul

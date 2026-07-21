#pragma once

#include <string>
#include <string_view>
#include <unordered_set>

namespace draxul
{

struct SemanticMegacityModel;

std::string exact_building_identity_key(
    std::string_view source_file_path,
    std::string_view module_path,
    std::string_view qualified_name);

std::unordered_set<std::string> connected_building_identities(
    const SemanticMegacityModel& model,
    std::string_view selected_source_file_path,
    std::string_view selected_module_path,
    std::string_view selected_qualified_name,
    std::string_view focus_entity_name = {});

} // namespace draxul

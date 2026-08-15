#include "city_selection.h"

#include "semantic_city_layout.h"

namespace draxul
{

std::string exact_building_identity_key(
    std::string_view source_file_path,
    std::string_view module_path,
    std::string_view qualified_name)
{
    std::string key;
    key.reserve(source_file_path.size() + module_path.size() + qualified_name.size() + 2);
    key.append(source_file_path);
    key.push_back('|');
    key.append(module_path);
    key.push_back('|');
    key.append(qualified_name);
    return key;
}

std::unordered_set<std::string> connected_building_identities(
    const SemanticMegacityModel& model,
    std::string_view selected_source_file_path,
    std::string_view selected_module_path,
    std::string_view selected_qualified_name,
    std::string_view focus_entity_name)
{
    const std::string selected_identity
        = exact_building_identity_key(selected_source_file_path, selected_module_path, selected_qualified_name);

    const auto resolve_identity = [&](const std::string& source_file_path, const std::string& module_path,
                                      const std::string& qualified_name) {
        std::string identity = exact_building_identity_key(source_file_path, module_path, qualified_name);
        if (identity == selected_identity)
            return identity;
        const auto function_bundle = model.function_bundle_remap.find(qualified_name);
        if (function_bundle != model.function_bundle_remap.end())
            return exact_building_identity_key("", module_path, function_bundle->second);
        const auto struct_stack = model.struct_stack_remap.find(qualified_name);
        if (struct_stack != model.struct_stack_remap.end())
            return exact_building_identity_key("", module_path, struct_stack->second);
        return identity;
    };

    std::unordered_set<std::string> connected;
    for (const auto& dependency : model.dependencies)
    {
        if (!focus_entity_name.empty()
            && dependency.source_qualified_name != focus_entity_name
            && dependency.target_qualified_name != focus_entity_name)
        {
            continue;
        }

        const std::string source_identity = resolve_identity(
            dependency.source_file_path,
            dependency.source_module_path,
            dependency.source_qualified_name);
        const std::string target_identity = resolve_identity(
            dependency.target_file_path,
            dependency.target_module_path,
            dependency.target_qualified_name);
        if (source_identity == selected_identity)
            connected.insert(target_identity);
        else if (target_identity == selected_identity)
            connected.insert(source_identity);
    }
    return connected;
}

} // namespace draxul

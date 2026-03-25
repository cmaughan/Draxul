#include "ui_treesitter_panel.h"
#include "semantic_city_layout.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace draxul
{

namespace
{

const char* symbol_kind_icon(SymbolKind kind)
{
    switch (kind)
    {
    case SymbolKind::Function:
        return "fn ";
    case SymbolKind::Class:
        return "cls";
    case SymbolKind::Struct:
        return "st ";
    case SymbolKind::Include:
        return "inc";
    }
    return "   ";
}

ImVec4 symbol_kind_color(SymbolKind kind)
{
    switch (kind)
    {
    case SymbolKind::Function:
        return { 0.60f, 0.85f, 1.00f, 1.0f }; // blue
    case SymbolKind::Class:
        return { 0.90f, 0.75f, 0.30f, 1.0f }; // gold
    case SymbolKind::Struct:
        return { 0.75f, 0.90f, 0.50f, 1.0f }; // green
    case SymbolKind::Include:
        return { 0.70f, 0.70f, 0.70f, 1.0f }; // grey
    }
    return { 1.0f, 1.0f, 1.0f, 1.0f };
}

// Returns the basename portion of a forward-slash path (no filesystem dependency).
std::string_view path_basename(std::string_view path)
{
    const auto pos = path.rfind('/');
    return pos == std::string_view::npos ? path : path.substr(pos + 1);
}

bool path_has_prefix(std::string_view path, std::string_view prefix)
{
    return path.rfind(prefix, 0) == 0;
}

std::string logical_module_for_file(std::string_view file_path)
{
    if (path_has_prefix(file_path, "libs/"))
    {
        const auto second_slash = file_path.find('/', 5);
        return second_slash == std::string_view::npos
            ? std::string(file_path)
            : std::string(file_path.substr(0, second_slash));
    }

    const auto first_slash = file_path.find('/');
    return first_slash == std::string_view::npos
        ? std::string(file_path)
        : std::string(file_path.substr(0, first_slash));
}

std::string file_path_within_module(std::string_view file_path)
{
    const std::string module = logical_module_for_file(file_path);
    if (module.empty() || file_path.size() <= module.size())
        return std::string(path_basename(file_path));

    std::string_view relative_path = file_path.substr(module.size());
    if (!relative_path.empty() && relative_path.front() == '/')
        relative_path.remove_prefix(1);
    return relative_path.empty() ? std::string(path_basename(file_path)) : std::string(relative_path);
}

float clamp_metric(float value, float min_value, float max_value)
{
    return std::clamp(value, min_value, max_value);
}

float maybe_clamp_metric(float value, float min_value, float max_value, bool clamp_metrics)
{
    return clamp_metrics ? clamp_metric(value, min_value, max_value) : value;
}

struct CityPreviewEntry
{
    std::string qualified_name;
    std::string module;
    std::string file;
    std::string module_file;
    bool is_tree = false;
    int base_size = 0;
    int function_count = 0;
    int function_mass = 0;
    int road_size = 0;
    float render_footprint = 1.0f;
    float render_height = 1.0f;
    float render_road = 0.6f;
};

float normalized_footprint(int base_size, bool clamp_metrics)
{
    return maybe_clamp_metric(
        1.0f + std::sqrt(static_cast<float>(std::max(base_size, 0))), 1.0f, 9.0f, clamp_metrics);
}

float normalized_height(int function_count, int function_mass, bool clamp_metrics)
{
    const float height = 2.0f
        + 1.35f * std::log1p(static_cast<float>(std::max(function_mass, 0)))
        + 0.45f * std::sqrt(static_cast<float>(std::max(function_count, 0)));
    return maybe_clamp_metric(height, 2.0f, 12.0f, clamp_metrics);
}

float normalized_road_width(int road_size, bool clamp_metrics)
{
    return maybe_clamp_metric(
        0.6f + 0.85f * std::log1p(static_cast<float>(std::max(road_size, 0))), 0.6f, 3.0f, clamp_metrics);
}

std::vector<CityPreviewEntry> build_city_preview(
    const CodebaseSnapshot& snap, bool clamp_metrics, bool hide_test_entities)
{
    std::unordered_set<std::string> concrete_type_names;
    std::unordered_map<std::string, std::vector<int>> method_sizes_by_type;
    for (const auto& file : snap.files)
    {
        for (const auto& sym : file.symbols)
        {
            if (sym.kind == SymbolKind::Function && !sym.parent.empty())
            {
                const int function_size = static_cast<int>(
                    sym.end_line >= sym.line ? (sym.end_line - sym.line + 1) : 1);
                method_sizes_by_type[sym.parent].push_back(function_size);
            }
        }
    }

    for (const auto& file : snap.files)
    {
        for (const auto& sym : file.symbols)
        {
            if (sym.is_abstract)
                continue;
            if (sym.kind == SymbolKind::Class)
                concrete_type_names.insert(sym.name);
            else if (sym.kind == SymbolKind::Struct
                && method_sizes_by_type.find(sym.name) != method_sizes_by_type.end())
                concrete_type_names.insert(sym.name);
        }
    }

    std::vector<CityPreviewEntry> entries;
    for (const auto& file : snap.files)
    {
        if (hide_test_entities && is_test_semantic_source(file.path))
            continue;
        const std::string module = logical_module_for_file(file.path);
        const std::string module_file = file_path_within_module(file.path);
        for (const auto& sym : file.symbols)
        {
            if (sym.kind == SymbolKind::Function && sym.parent.empty())
            {
                const int function_size = static_cast<int>(
                    sym.end_line >= sym.line ? (sym.end_line - sym.line + 1) : 1);
                CityPreviewEntry entry;
                entry.qualified_name = sym.name;
                entry.module = module;
                entry.file = file.path;
                entry.module_file = module_file;
                entry.is_tree = true;
                entry.function_count = 1;
                entry.function_mass = function_size;
                entry.render_footprint = 1.0f;
                entry.render_height = maybe_clamp_metric(
                    1.4f + 0.9f * std::log1p(static_cast<float>(function_size)), 1.4f, 4.5f, clamp_metrics);
                entry.render_road = 0.0f;
                entries.push_back(std::move(entry));
                continue;
            }

            const auto method_it = method_sizes_by_type.find(sym.name);
            const bool concrete_class = sym.kind == SymbolKind::Class && !sym.is_abstract;
            const bool concrete_struct = sym.kind == SymbolKind::Struct
                && !sym.is_abstract
                && method_it != method_sizes_by_type.end();
            if (!concrete_class && !concrete_struct)
                continue;

            CityPreviewEntry entry;
            entry.qualified_name = sym.name;
            entry.module = module;
            entry.file = file.path;
            entry.module_file = module_file;
            entry.base_size = static_cast<int>(sym.field_count);
            if (method_it != method_sizes_by_type.end())
            {
                entry.function_count = static_cast<int>(method_it->second.size());
                for (const int size : method_it->second)
                    entry.function_mass += size;
            }

            std::unordered_set<std::string> external_refs;
            for (const auto& ref : sym.referenced_types)
            {
                if (ref != sym.name && concrete_type_names.find(ref) != concrete_type_names.end())
                    external_refs.insert(ref);
            }
            entry.road_size = static_cast<int>(external_refs.size());
            entry.render_footprint = normalized_footprint(entry.base_size, clamp_metrics);
            entry.render_height = normalized_height(entry.function_count, entry.function_mass, clamp_metrics);
            entry.render_road = normalized_road_width(entry.road_size, clamp_metrics);
            entries.push_back(std::move(entry));
        }
    }

    std::sort(entries.begin(), entries.end(), [](const CityPreviewEntry& a, const CityPreviewEntry& b) {
        if (a.module != b.module)
            return a.module < b.module;
        return a.qualified_name < b.qualified_name;
    });

    return entries;
}

void render_city_preview(const CodebaseSnapshot& snap, bool clamp_metrics, bool hide_test_entities)
{
    const std::vector<CityPreviewEntry> entries = build_city_preview(snap, clamp_metrics, hide_test_entities);
    if (entries.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        ImGui::TextUnformatted("No city entities yet.");
        ImGui::PopStyleColor();
        return;
    }

    int building_count = 0;
    int tree_count = 0;
    for (const auto& entry : entries)
    {
        if (entry.is_tree)
            ++tree_count;
        else
            ++building_count;
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
    ImGui::Text("Compressed preview: %d buildings | %d trees", building_count, tree_count);
    ImGui::TextUnformatted(clamp_metrics
            ? "Render metrics use sqrt/log compression and clamped city-friendly ranges."
            : "Render metrics use sqrt/log compression with all clamps disabled.");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    std::map<std::string, std::vector<const CityPreviewEntry*>> modules;
    for (const auto& entry : entries)
        modules[entry.module].push_back(&entry);

    for (const auto& [module, module_entries] : modules)
    {
        const std::string label = module.empty()
            ? "Module: (root)"
            : ("Module: " + module);
        const bool module_open = ImGui::TreeNodeEx(
            label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth,
            "%s (%zu)", label.c_str(), module_entries.size());
        if (!module_open)
            continue;

        for (const CityPreviewEntry* entry : module_entries)
        {
            const ImVec4 color = entry->is_tree
                ? ImVec4(0.55f, 0.88f, 0.55f, 1.0f)
                : ImVec4(0.88f, 0.82f, 0.55f, 1.0f);
            const char* kind = entry->is_tree ? "tree" : "building";

            ImGui::PushStyleColor(ImGuiCol_Text, color);
            const bool open = ImGui::TreeNodeEx(
                (entry->qualified_name + entry->file).c_str(),
                ImGuiTreeNodeFlags_SpanAvailWidth,
                "%s [%s]  (%s)",
                entry->qualified_name.c_str(),
                kind,
                entry->module_file.c_str());
            ImGui::PopStyleColor();

            if (!open)
                continue;

            ImGui::Text("file: %s", entry->file.c_str());
            if (entry->is_tree)
            {
                ImGui::Text("raw: function_mass=%d", entry->function_mass);
                ImGui::Text("render: height=%.2f", entry->render_height);
            }
            else
            {
                ImGui::Text(
                    "raw: base=%d | methods=%d | function_mass=%d | roads=%d",
                    entry->base_size,
                    entry->function_count,
                    entry->function_mass,
                    entry->road_size);
                ImGui::Text(
                    "render: footprint=%.2f x %.2f | height=%.2f | road=%.2f",
                    entry->render_footprint,
                    entry->render_footprint,
                    entry->render_height,
                    entry->render_road);
            }

            ImGui::TreePop();
        }

        ImGui::TreePop();
    }
}

void render_stats(const CodebaseSnapshot& snap)
{
    size_t fn_count = 0, cls_count = 0, st_count = 0, err_count = 0;
    for (const auto& f : snap.files)
    {
        for (const auto& sym : f.symbols)
        {
            if (sym.kind == SymbolKind::Function)
                ++fn_count;
            else if (sym.kind == SymbolKind::Class)
                ++cls_count;
            else if (sym.kind == SymbolKind::Struct)
                ++st_count;
        }
        err_count += f.errors.size();
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
    ImGui::Text(
        "%zu files  |  %zu functions  |  %zu classes  |  %zu structs",
        snap.files.size(), fn_count, cls_count, st_count);
    if (err_count > 0)
    {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
        ImGui::Text("  |  %zu parse errors", err_count);
        ImGui::PopStyleColor();
    }
    ImGui::PopStyleColor();
}

// ---- Files root ----------------------------------------------------------------

void render_files_tree(const CodebaseSnapshot& snap)
{
    for (const auto& file : snap.files)
    {
        const bool has_children = !file.symbols.empty() || !file.errors.empty();
        const ImGuiTreeNodeFlags file_flags = ImGuiTreeNodeFlags_SpanAvailWidth
            | (has_children ? 0 : ImGuiTreeNodeFlags_Leaf);

        const ImVec4 file_color = file.errors.empty()
            ? ImVec4(0.85f, 0.85f, 0.85f, 1.0f)
            : ImVec4(1.0f, 0.55f, 0.55f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, file_color);
        const bool open = ImGui::TreeNodeEx(
            file.path.c_str(), file_flags, "%s", file.path.c_str());
        ImGui::PopStyleColor();

        if (open)
        {
            if (!file.errors.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                const bool err_open = ImGui::TreeNodeEx(
                    "##parse_errors",
                    ImGuiTreeNodeFlags_SpanAvailWidth,
                    "Parse errors (%zu)",
                    file.errors.size());
                ImGui::PopStyleColor();
                if (err_open)
                {
                    for (const auto& err : file.errors)
                    {
                        ImGui::PushStyleColor(
                            ImGuiCol_Text, ImVec4(1.0f, 0.65f, 0.65f, 1.0f));
                        ImGui::TreeNodeEx(
                            (void*)(uintptr_t)(&err),
                            ImGuiTreeNodeFlags_Leaf
                                | ImGuiTreeNodeFlags_NoTreePushOnOpen
                                | ImGuiTreeNodeFlags_SpanAvailWidth,
                            "line %u  col %u",
                            err.line,
                            err.col);
                        ImGui::PopStyleColor();
                    }
                    ImGui::TreePop();
                }
            }

            for (const auto& sym : file.symbols)
            {
                if (sym.kind == SymbolKind::Include)
                    continue;

                ImGui::PushStyleColor(ImGuiCol_Text, symbol_kind_color(sym.kind));
                ImGui::TreeNodeEx(
                    sym.name.c_str(),
                    ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen
                        | ImGuiTreeNodeFlags_SpanAvailWidth,
                    "[%s] %s  :%u",
                    symbol_kind_icon(sym.kind),
                    sym.name.c_str(),
                    sym.line);
                ImGui::PopStyleColor();
            }
            ImGui::TreePop();
        }
    }
}

// ---- Objects root --------------------------------------------------------------

struct ClassEntry
{
    std::string name;
    std::string file;
    uint32_t line;
    SymbolKind kind; // Class or Struct
};

struct FuncEntry
{
    std::string name;
    std::string file;
    uint32_t line;
};

struct ModuleObjects
{
    std::vector<ClassEntry> concrete;
    std::vector<ClassEntry> abstract;
    std::vector<ClassEntry> data_structs;
    std::vector<FuncEntry> free_functions;
};

void render_symbol_leaf(const char* id, const char* icon, ImVec4 color,
    std::string_view name, std::string_view file, uint32_t line)
{
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TreeNodeEx(
        id,
        ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen
            | ImGuiTreeNodeFlags_SpanAvailWidth,
        "[%s] %.*s  (%.*s:%u)",
        icon,
        (int)name.size(), name.data(),
        (int)file.size(), file.data(),
        line);
    ImGui::PopStyleColor();
}

void render_objects_tree(const CodebaseSnapshot& snap)
{
    // Build set of type names that have member functions
    std::set<std::string> types_with_methods;
    for (const auto& file : snap.files)
    {
        for (const auto& sym : file.symbols)
        {
            if (sym.kind == SymbolKind::Function && !sym.parent.empty())
                types_with_methods.insert(sym.parent);
        }
    }

    // Collect across all files by logical module root so include/src live together.
    std::map<std::string, ModuleObjects> modules;
    for (const auto& file : snap.files)
    {
        const std::string module = logical_module_for_file(file.path);
        const std::string module_file = file_path_within_module(file.path);
        auto& module_entries = modules[module];
        for (const auto& sym : file.symbols)
        {
            if (sym.kind == SymbolKind::Class || sym.kind == SymbolKind::Struct)
            {
                ClassEntry e{ sym.name, module_file, sym.line, sym.kind };
                if (sym.is_abstract)
                    module_entries.abstract.push_back(std::move(e));
                else if (sym.kind == SymbolKind::Struct
                    && types_with_methods.find(sym.name) == types_with_methods.end())
                    module_entries.data_structs.push_back(std::move(e));
                else
                    module_entries.concrete.push_back(std::move(e));
            }
            else if (sym.kind == SymbolKind::Function && sym.parent.empty())
            {
                module_entries.free_functions.push_back({ sym.name, module_file, sym.line });
            }
        }
    }

    auto by_name_then_file = [](const auto& a, const auto& b) {
        if (a.name != b.name)
            return a.name < b.name;
        return a.file < b.file;
    };
    size_t concrete_count = 0;
    size_t abstract_count = 0;
    size_t struct_count = 0;
    size_t function_count = 0;
    for (auto& [module, module_entries] : modules)
    {
        std::sort(module_entries.concrete.begin(), module_entries.concrete.end(), by_name_then_file);
        std::sort(module_entries.abstract.begin(), module_entries.abstract.end(), by_name_then_file);
        std::sort(
            module_entries.data_structs.begin(), module_entries.data_structs.end(), by_name_then_file);
        std::sort(
            module_entries.free_functions.begin(),
            module_entries.free_functions.end(),
            by_name_then_file);
        concrete_count += module_entries.concrete.size();
        abstract_count += module_entries.abstract.size();
        struct_count += module_entries.data_structs.size();
        function_count += module_entries.free_functions.size();
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
    ImGui::Text(
        "%zu modules  |  %zu concrete classes  |  %zu functions  |  %zu abstract classes  |  %zu data structs",
        modules.size(),
        concrete_count,
        function_count,
        abstract_count,
        struct_count);
    ImGui::PopStyleColor();

    for (const auto& [module, module_entries] : modules)
    {
        const std::string label = module.empty() ? "Module: (root)" : ("Module: " + module);
        const size_t entity_count = module_entries.concrete.size()
            + module_entries.free_functions.size()
            + module_entries.abstract.size()
            + module_entries.data_structs.size();
        const bool module_open = ImGui::TreeNodeEx(
            label.c_str(),
            ImGuiTreeNodeFlags_SpanAvailWidth,
            "%s (%zu)",
            label.c_str(),
            entity_count);
        if (!module_open)
            continue;

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
        const bool concrete_open = ImGui::TreeNodeEx(
            (label + "##concrete").c_str(),
            ImGuiTreeNodeFlags_SpanAvailWidth,
            "Concrete Classes (%zu)",
            module_entries.concrete.size());
        ImGui::PopStyleColor();
        if (concrete_open)
        {
            for (const auto& e : module_entries.concrete)
            {
                render_symbol_leaf(
                    (module + e.name + e.file).c_str(),
                    symbol_kind_icon(e.kind),
                    symbol_kind_color(e.kind),
                    e.name,
                    e.file,
                    e.line);
            }
            ImGui::TreePop();
        }

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.85f, 1.00f, 1.0f));
        const bool fn_open = ImGui::TreeNodeEx(
            (label + "##functions").c_str(),
            ImGuiTreeNodeFlags_SpanAvailWidth,
            "Functions (%zu)",
            module_entries.free_functions.size());
        ImGui::PopStyleColor();
        if (fn_open)
        {
            for (const auto& e : module_entries.free_functions)
            {
                render_symbol_leaf(
                    (module + e.name + e.file).c_str(),
                    "fn ",
                    { 0.60f, 0.85f, 1.00f, 1.0f },
                    e.name,
                    e.file,
                    e.line);
            }
            ImGui::TreePop();
        }

        const size_t other_count = module_entries.abstract.size() + module_entries.data_structs.size();
        if (other_count > 0)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.72f, 0.72f, 0.72f, 1.0f));
            const bool other_open = ImGui::TreeNodeEx(
                (label + "##other").c_str(),
                ImGuiTreeNodeFlags_SpanAvailWidth,
                "Other Symbols (%zu)",
                other_count);
            ImGui::PopStyleColor();
            if (other_open)
            {
                if (!module_entries.abstract.empty())
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.65f, 0.40f, 1.0f));
                    const bool abs_open = ImGui::TreeNodeEx(
                        (label + "##abstract").c_str(),
                        ImGuiTreeNodeFlags_SpanAvailWidth,
                        "Abstract Classes (%zu)",
                        module_entries.abstract.size());
                    ImGui::PopStyleColor();
                    if (abs_open)
                    {
                        for (const auto& e : module_entries.abstract)
                        {
                            render_symbol_leaf(
                                (module + e.name + e.file).c_str(),
                                symbol_kind_icon(e.kind),
                                symbol_kind_color(e.kind),
                                e.name,
                                e.file,
                                e.line);
                        }
                        ImGui::TreePop();
                    }
                }

                if (!module_entries.data_structs.empty())
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.90f, 0.50f, 1.0f));
                    const bool structs_open = ImGui::TreeNodeEx(
                        (label + "##structs").c_str(),
                        ImGuiTreeNodeFlags_SpanAvailWidth,
                        "Data Structs (%zu)",
                        module_entries.data_structs.size());
                    ImGui::PopStyleColor();
                    if (structs_open)
                    {
                        for (const auto& e : module_entries.data_structs)
                        {
                            render_symbol_leaf(
                                (module + e.name + e.file).c_str(),
                                "st ",
                                { 0.75f, 0.90f, 0.50f, 1.0f },
                                e.name,
                                e.file,
                                e.line);
                        }
                        ImGui::TreePop();
                    }
                }

                ImGui::TreePop();
            }
        }

        ImGui::TreePop();
    }
}

bool render_renderer_controls(MegacityRendererControls& controls)
{
    bool changed = false;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
    const bool renderer_open = ImGui::TreeNodeEx(
        "##renderer_root", ImGuiTreeNodeFlags_SpanAvailWidth, "Renderer");
    ImGui::PopStyleColor();
    if (!renderer_open)
        return false;

    float hidden_px = controls.sign_text_hidden_px;
    float full_px = controls.sign_text_full_px;
    float output_gamma = controls.output_gamma;
    bool clamp_semantic_metrics = controls.clamp_semantic_metrics;
    bool hide_test_entities = controls.hide_test_entities;
    changed |= ImGui::SliderFloat("Sign Text Hidden <= px", &hidden_px, 0.0f, 64.0f, "%.1f");
    changed |= ImGui::SliderFloat("Sign Text Full >= px", &full_px, 0.0f, 64.0f, "%.1f");
    changed |= ImGui::SliderFloat("Output Gamma", &output_gamma, 1.0f, 3.0f, "%.2f");
    changed |= ImGui::Checkbox("Clamp Semantic Metrics", &clamp_semantic_metrics);
    changed |= ImGui::Checkbox("Hide Test Entities", &hide_test_entities);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.68f, 0.68f, 0.68f, 1.0f));
    ImGui::TextUnformatted("Fade uses projected ink size on screen, not camera distance.");
    ImGui::TextUnformatted("Gamma is a Megacity-only final output curve, not true sRGB backbuffer conversion.");
    ImGui::PopStyleColor();

    controls.sign_text_hidden_px = std::max(hidden_px, 0.0f);
    controls.sign_text_full_px = std::max(full_px, 0.0f);
    controls.output_gamma = std::max(output_gamma, 1.0f);
    controls.clamp_semantic_metrics = clamp_semantic_metrics;
    controls.hide_test_entities = hide_test_entities;

    ImGui::TreePop();
    return changed;
}

} // namespace

// ---- Public entry point --------------------------------------------------------

bool render_treesitter_panel(
    int window_w,
    int window_h,
    const std::shared_ptr<const CodebaseSnapshot>& snapshot,
    MegacityRendererControls* renderer_controls)
{
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoBringToFrontOnFocus;
    bool changed = false;

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(
        ImVec2(static_cast<float>(window_w) * 0.5f, static_cast<float>(window_h)),
        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 3.0f));

    if (!ImGui::Begin("Codebase Analysis", nullptr, flags))
    {
        ImGui::PopStyleVar(2);
        ImGui::End();
        return false;
    }

    if (!snapshot)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        ImGui::Text("(starting...)");
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        ImGui::End();
        return false;
    }

    if (!snapshot->complete)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.9f, 0.6f, 1.0f));
        ImGui::Text("Scanning... %zu files", snapshot->files.size());
        ImGui::PopStyleColor();
    }

    ImGui::Separator();
    render_stats(*snapshot);
    ImGui::Separator();

    ImGui::BeginChild("##content", ImVec2(0.0f, 0.0f), false,
        ImGuiWindowFlags_HorizontalScrollbar);

    bool clamp_semantic_metrics = true;
    bool hide_test_entities = true;
    if (renderer_controls)
    {
        changed |= render_renderer_controls(*renderer_controls);
        clamp_semantic_metrics = renderer_controls->clamp_semantic_metrics;
        hide_test_entities = renderer_controls->hide_test_entities;
    }

    // ---- Files root ------------------------------------------------------------
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
    const bool files_open = ImGui::TreeNodeEx(
        "##files_root", ImGuiTreeNodeFlags_SpanAvailWidth,
        "Files (%zu)", snapshot->files.size());
    ImGui::PopStyleColor();
    if (files_open)
    {
        render_files_tree(*snapshot);
        ImGui::TreePop();
    }

    // ---- Objects root ----------------------------------------------------------
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
    const bool objects_open = ImGui::TreeNodeEx(
        "##objects_root", ImGuiTreeNodeFlags_SpanAvailWidth,
        "Objects");
    ImGui::PopStyleColor();
    if (objects_open)
    {
        render_objects_tree(*snapshot);
        ImGui::TreePop();
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
    const bool city_open = ImGui::TreeNodeEx(
        "##city_preview_root", ImGuiTreeNodeFlags_SpanAvailWidth,
        "City Preview");
    ImGui::PopStyleColor();
    if (city_open)
    {
        render_city_preview(*snapshot, clamp_semantic_metrics, hide_test_entities);
        ImGui::TreePop();
    }

    ImGui::EndChild();

    ImGui::PopStyleVar(2);
    ImGui::End();
    return changed;
}

} // namespace draxul

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
    const std::string module_path = logical_module_for_file(file_path);
    if (module_path.empty() || file_path.size() <= module_path.size())
        return std::string(path_basename(file_path));

    std::string_view relative_path = file_path.substr(module_path.size());
    if (!relative_path.empty() && relative_path.front() == '/')
        relative_path.remove_prefix(1);
    return relative_path.empty() ? std::string(path_basename(file_path)) : std::string(relative_path);
}

std::string file_path_within_module(std::string_view file_path, std::string_view module_path)
{
    if (module_path.empty() || file_path.size() <= module_path.size())
        return std::string(path_basename(file_path));

    std::string_view relative_path = file_path;
    if (path_has_prefix(file_path, module_path))
    {
        relative_path = file_path.substr(module_path.size());
        if (!relative_path.empty() && relative_path.front() == '/')
            relative_path.remove_prefix(1);
    }

    return relative_path.empty() ? std::string(path_basename(file_path)) : std::string(relative_path);
}

struct SnapshotStats
{
    size_t file_count = 0;
    size_t function_count = 0;
    size_t class_count = 0;
    size_t struct_count = 0;
    size_t error_count = 0;
};

struct FilesSymbolEntry
{
    std::string name;
    SymbolKind kind = SymbolKind::Function;
    uint32_t line = 0;
};

struct FilesErrorEntry
{
    uint32_t line = 0;
    uint32_t col = 0;
};

struct FilesEntry
{
    std::string path;
    bool has_children = false;
    bool has_errors = false;
    std::vector<FilesErrorEntry> errors;
    std::vector<FilesSymbolEntry> symbols;
};

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

struct ObjectsTreeCache
{
    std::map<std::string, ModuleObjects> modules;
    size_t concrete_count = 0;
    size_t abstract_count = 0;
    size_t struct_count = 0;
    size_t function_count = 0;
};

struct SnapshotUiCache
{
    std::shared_ptr<const CodebaseSnapshot> snapshot;
    SnapshotStats stats;
    std::vector<FilesEntry> files;
    ObjectsTreeCache objects;
    bool valid = false;
};

bool same_snapshot_identity(
    const std::shared_ptr<const CodebaseSnapshot>& cached,
    const std::shared_ptr<const CodebaseSnapshot>& current)
{
    return cached
        && cached.get() == current.get()
        && cached->scan_time == current->scan_time
        && cached->complete == current->complete
        && cached->files.size() == current->files.size();
}

SnapshotStats build_snapshot_stats(const CodebaseSnapshot& snap)
{
    SnapshotStats stats;
    stats.file_count = snap.files.size();
    for (const auto& f : snap.files)
    {
        for (const auto& sym : f.symbols)
        {
            if (sym.kind == SymbolKind::Function)
                ++stats.function_count;
            else if (sym.kind == SymbolKind::Class)
                ++stats.class_count;
            else if (sym.kind == SymbolKind::Struct)
                ++stats.struct_count;
        }
        stats.error_count += f.errors.size();
    }
    return stats;
}

std::vector<FilesEntry> build_files_entries(const CodebaseSnapshot& snap)
{
    std::vector<FilesEntry> files;
    files.reserve(snap.files.size());
    for (const auto& file : snap.files)
    {
        FilesEntry entry;
        entry.path = file.path;
        entry.has_children = !file.symbols.empty() || !file.errors.empty();
        entry.has_errors = !file.errors.empty();
        entry.errors.reserve(file.errors.size());
        for (const auto& err : file.errors)
            entry.errors.push_back({ err.line, err.col });
        entry.symbols.reserve(file.symbols.size());
        for (const auto& sym : file.symbols)
        {
            if (sym.kind == SymbolKind::Include)
                continue;
            entry.symbols.push_back({ sym.name, sym.kind, sym.line });
        }
        files.push_back(std::move(entry));
    }
    return files;
}

ObjectsTreeCache build_objects_tree_cache(const CodebaseSnapshot& snap)
{
    ObjectsTreeCache cache;

    std::set<std::string> types_with_methods;
    for (const auto& file : snap.files)
    {
        for (const auto& sym : file.symbols)
        {
            if (sym.kind == SymbolKind::Function && !sym.parent.empty())
                types_with_methods.insert(sym.parent);
        }
    }

    for (const auto& file : snap.files)
    {
        const std::string module_path = logical_module_for_file(file.path);
        const std::string module_file = file_path_within_module(file.path);
        auto& module_entries = cache.modules[module_path];
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
    for (auto& [module_path, module_entries] : cache.modules)
    {
        std::sort(module_entries.concrete.begin(), module_entries.concrete.end(), by_name_then_file);
        std::sort(module_entries.abstract.begin(), module_entries.abstract.end(), by_name_then_file);
        std::sort(module_entries.data_structs.begin(), module_entries.data_structs.end(), by_name_then_file);
        std::sort(module_entries.free_functions.begin(), module_entries.free_functions.end(), by_name_then_file);
        cache.concrete_count += module_entries.concrete.size();
        cache.abstract_count += module_entries.abstract.size();
        cache.struct_count += module_entries.data_structs.size();
        cache.function_count += module_entries.free_functions.size();
    }

    return cache;
}

const SnapshotUiCache& cached_snapshot_ui(
    const std::shared_ptr<const CodebaseSnapshot>& snapshot)
{
    static SnapshotUiCache cache;
    if (!snapshot)
        return cache;

    if (!cache.valid || !same_snapshot_identity(cache.snapshot, snapshot))
    {
        cache.snapshot = snapshot;
        cache.stats = build_snapshot_stats(*snapshot);
        cache.files = build_files_entries(*snapshot);
        cache.objects = build_objects_tree_cache(*snapshot);
        cache.valid = true;
    }

    return cache;
}

void render_city_preview(const SemanticMegacityModel* semantic_model)
{
    if (!semantic_model || semantic_model->empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        ImGui::TextUnformatted("No city entities yet.");
        ImGui::PopStyleColor();
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
    ImGui::Text(
        "Semantic preview: %zu modules | %zu buildings",
        semantic_model->modules.size(),
        semantic_model->building_count());
    ImGui::TextUnformatted("Preview reflects the last rebuilt DB-derived city model.");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    for (const auto& module_model : semantic_model->modules)
    {
        const std::string label = module_model.module_path.empty()
            ? "Module: (root)"
            : ("Module: " + module_model.module_path);
        const bool module_open = ImGui::TreeNodeEx(
            label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth,
            "%s (%zu)", label.c_str(), module_model.buildings.size());
        if (!module_open)
            continue;

        for (const SemanticCityBuilding& building : module_model.buildings)
        {
            const ImVec4 color = ImVec4(0.88f, 0.82f, 0.55f, 1.0f);
            const std::string module_file = file_path_within_module(building.source_file_path, module_model.module_path);

            ImGui::PushStyleColor(ImGuiCol_Text, color);
            const bool open = ImGui::TreeNodeEx(
                (building.qualified_name + building.source_file_path).c_str(),
                ImGuiTreeNodeFlags_SpanAvailWidth,
                "%s [building]  (%s)",
                building.qualified_name.c_str(),
                module_file.c_str());
            ImGui::PopStyleColor();

            if (!open)
                continue;

            ImGui::Text("file: %s", building.source_file_path.c_str());
            ImGui::Text(
                "raw: base=%d | methods=%d | function_mass=%d | roads=%d",
                building.base_size,
                building.function_count,
                building.function_mass,
                building.road_size);
            ImGui::Text(
                "render: footprint=%.2f x %.2f | height=%.2f | road=%.2f",
                building.metrics.footprint,
                building.metrics.footprint,
                building.metrics.height,
                building.metrics.road_width);

            ImGui::TreePop();
        }

        ImGui::TreePop();
    }
}

void render_stats(const SnapshotStats& stats)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
    ImGui::Text(
        "%zu files  |  %zu functions  |  %zu classes  |  %zu structs",
        stats.file_count, stats.function_count, stats.class_count, stats.struct_count);
    if (stats.error_count > 0)
    {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
        ImGui::Text("  |  %zu parse errors", stats.error_count);
        ImGui::PopStyleColor();
    }
    ImGui::PopStyleColor();
}

// ---- Files root ----------------------------------------------------------------

void render_files_tree(const std::vector<FilesEntry>& files)
{
    for (const auto& file : files)
    {
        const ImGuiTreeNodeFlags file_flags = ImGuiTreeNodeFlags_SpanAvailWidth
            | (file.has_children ? 0 : ImGuiTreeNodeFlags_Leaf);

        const ImVec4 file_color = file.has_errors
            ? ImVec4(1.0f, 0.55f, 0.55f, 1.0f)
            : ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
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

void render_objects_tree(const ObjectsTreeCache& cache)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
    ImGui::Text(
        "%zu modules  |  %zu concrete classes  |  %zu functions  |  %zu abstract classes  |  %zu data structs",
        cache.modules.size(),
        cache.concrete_count,
        cache.function_count,
        cache.abstract_count,
        cache.struct_count);
    ImGui::PopStyleColor();

    for (const auto& [module_path, module_entries] : cache.modules)
    {
        const std::string label = module_path.empty() ? "Module: (root)" : ("Module: " + module_path);
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
                    (module_path + e.name + e.file).c_str(),
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
                    (module_path + e.name + e.file).c_str(),
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
                                (module_path + e.name + e.file).c_str(),
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
                                (module_path + e.name + e.file).c_str(),
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
    controls.rebuild_requested = false;
    controls.reset_camera_requested = false;
    controls.committed_edit = false;
    controls.set_defaults_requested = false;
    MegaCityCodeConfig config = controls.config;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
    const bool renderer_open = ImGui::TreeNodeEx(
        "##renderer_root", ImGuiTreeNodeFlags_SpanAvailWidth, "Renderer");
    ImGui::PopStyleColor();
    if (!renderer_open)
        return false;

    if (controls.rebuild_pending)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.78f, 0.35f, 1.0f));
        ImGui::TextUnformatted("World rebuild pending.");
        ImGui::PopStyleColor();
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.90f, 0.60f, 1.0f));
        ImGui::TextUnformatted("World is current.");
        ImGui::PopStyleColor();
    }

    if (ImGui::Button("Rebuild World"))
        controls.rebuild_requested = true;
    ImGui::SameLine();
    if (ImGui::Button("Reset Camera"))
        controls.reset_camera_requested = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("Auto Rebuild", &config.auto_rebuild))
    {
        changed = true;
        controls.committed_edit = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Config"))
    {
        config = controls.defaults;
        changed = true;
        controls.committed_edit = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Set Defaults"))
        ImGui::OpenPopup("##megacity_set_defaults");

    if (ImGui::BeginPopupModal("##megacity_set_defaults", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Use the current Megacity config as the new defaults?");
        if (ImGui::Button("Set Defaults"))
        {
            controls.defaults = config;
            controls.set_defaults_requested = true;
            changed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    auto note_commit = [&]() {
        if (ImGui::IsItemDeactivatedAfterEdit())
            controls.committed_edit = true;
    };
    auto edit_float = [&](const char* label, float& value, float speed, float min_value, float max_value, const char* format) {
        const bool item_changed = ImGui::DragFloat(label, &value, speed, min_value, max_value, format);
        changed |= item_changed;
        note_commit();
    };
    auto edit_int = [&](const char* label, int& value, int speed, int min_value, int max_value) {
        const bool item_changed = ImGui::DragInt(label, &value, static_cast<float>(speed), min_value, max_value);
        changed |= item_changed;
        note_commit();
    };

    if (ImGui::TreeNodeEx("##renderer_build", ImGuiTreeNodeFlags_SpanAvailWidth, "City Build"))
    {
        const bool clamp_changed = ImGui::Checkbox("Clamp Semantic Metrics", &config.clamp_semantic_metrics);
        changed |= clamp_changed;
        if (clamp_changed)
            controls.committed_edit = true;
        const bool hide_tests_changed = ImGui::Checkbox("Hide Test Entities", &config.hide_test_entities);
        changed |= hide_tests_changed;
        if (hide_tests_changed)
            controls.committed_edit = true;
        edit_float("Height Multiplier", config.height_multiplier, 0.05f, 0.1f, 8.0f, "%.2f");
        edit_float("Placement Step", config.placement_step, 0.01f, 0.05f, 8.0f, "%.2f");
        edit_int("Max Spiral Rings", config.max_spiral_rings, 8, 8, 65536);
        edit_float("Lot Road Reserve", config.lot_road_reserve_fraction, 0.01f, 0.0f, 4.0f, "%.2f");
        edit_float("Footprint Base", config.footprint_base, 0.05f, 0.0f, 32.0f, "%.2f");
        edit_float("Footprint Min", config.footprint_min, 0.05f, 0.0f, 32.0f, "%.2f");
        edit_float("Footprint Max", config.footprint_max, 0.05f, 0.0f, 64.0f, "%.2f");
        edit_float("Footprint Unclamped Scale", config.footprint_unclamped_scale, 0.01f, 0.0f, 4.0f, "%.2f");
        edit_float("Height Base", config.height_base, 0.05f, 0.0f, 32.0f, "%.2f");
        edit_float("Height Mass Weight", config.height_mass_weight, 0.01f, 0.0f, 8.0f, "%.2f");
        edit_float("Height Count Weight", config.height_count_weight, 0.01f, 0.0f, 8.0f, "%.2f");
        edit_float("Height Min", config.height_min, 0.05f, 0.0f, 32.0f, "%.2f");
        edit_float("Height Max", config.height_max, 0.05f, 0.0f, 64.0f, "%.2f");
        edit_float("Height Unclamped Count Weight", config.height_unclamped_count_weight, 0.01f, 0.0f, 8.0f, "%.2f");
        edit_float("Road Width Base", config.road_width_base, 0.01f, 0.0f, 16.0f, "%.2f");
        edit_float("Road Width Scale", config.road_width_scale, 0.01f, 0.0f, 8.0f, "%.2f");
        edit_float("Road Width Min", config.road_width_min, 0.01f, 0.0f, 16.0f, "%.2f");
        edit_float("Road Width Max", config.road_width_max, 0.01f, 0.0f, 32.0f, "%.2f");
        edit_float("Sidewalk Width", config.sidewalk_width, 0.01f, 0.0f, 16.0f, "%.2f");
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("##renderer_signs", ImGuiTreeNodeFlags_SpanAvailWidth, "Signs"))
    {
        edit_float("Sign Text Hidden <= px", config.sign_text_hidden_px, 0.1f, 0.0f, 64.0f, "%.1f");
        edit_float("Sign Text Full >= px", config.sign_text_full_px, 0.1f, 0.0f, 64.0f, "%.1f");
        edit_float("Sign Label Point Size", config.sign_label_point_size, 0.25f, 1.0f, 72.0f, "%.1f");
        edit_int("Wall Sign Text Padding", config.wall_sign_text_padding, 1, 0, 64);

        static constexpr std::array<const char*, 8> kPlacementLabels = {
            "Roof North",
            "Roof South",
            "Roof East",
            "Roof West",
            "Wall North",
            "Wall South",
            "Wall East",
            "Wall West",
        };
        int placement = static_cast<int>(config.building_sign_placement);
        if (ImGui::Combo("Building Sign Placement", &placement, kPlacementLabels.data(), static_cast<int>(kPlacementLabels.size())))
        {
            config.building_sign_placement = static_cast<MegaCitySignPlacement>(std::clamp(placement, 0, 7));
            changed = true;
            controls.committed_edit = true;
        }

        edit_float("Roof Sign Thickness", config.roof_sign_thickness, 0.005f, 0.001f, 2.0f, "%.3f");
        edit_float("Roof Sign Depth", config.roof_sign_depth, 0.01f, 0.01f, 8.0f, "%.2f");
        edit_float("Roof Sign Edge Inset", config.roof_sign_edge_inset, 0.01f, 0.0f, 4.0f, "%.2f");
        edit_float("Roof Sign Side Inset", config.roof_sign_side_inset, 0.01f, 0.0f, 4.0f, "%.2f");
        edit_float("Wall Sign Thickness", config.wall_sign_thickness, 0.005f, 0.001f, 2.0f, "%.3f");
        edit_float("Wall Sign Face Gap", config.wall_sign_face_gap, 0.001f, 0.0f, 1.0f, "%.3f");
        edit_float("Wall Sign Width", config.wall_sign_width, 0.01f, 0.05f, 16.0f, "%.2f");
        edit_float("Wall Sign Side Inset", config.wall_sign_side_inset, 0.01f, 0.0f, 4.0f, "%.2f");
        edit_float("Wall Sign Top Inset", config.wall_sign_top_inset, 0.01f, 0.0f, 8.0f, "%.2f");
        edit_float("Wall Sign Bottom Inset", config.wall_sign_bottom_inset, 0.01f, 0.0f, 8.0f, "%.2f");
        edit_float("Road Sign Edge Inset", config.road_sign_edge_inset, 0.01f, 0.0f, 4.0f, "%.2f");
        edit_float("Road Sign Side Inset", config.road_sign_side_inset, 0.01f, 0.0f, 4.0f, "%.2f");
        edit_float("Minimum Road Sign Depth", config.minimum_road_sign_depth, 0.01f, 0.01f, 8.0f, "%.2f");
        edit_float("Sidewalk Sign Edge Inset", config.sidewalk_sign_edge_inset, 0.01f, 0.0f, 4.0f, "%.2f");
        edit_float("Road Sign Lift", config.road_sign_lift, 0.001f, 0.0f, 1.0f, "%.3f");
        edit_float("Sign Pixels / World Unit", config.roof_sign_pixels_per_world_unit, 1.0f, 8.0f, 2048.0f, "%.1f");
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("##renderer_surface", ImGuiTreeNodeFlags_SpanAvailWidth, "Surfaces"))
    {
        edit_float("Road Surface Height", config.road_surface_height, 0.001f, 0.001f, 4.0f, "%.3f");
        edit_float("Sidewalk Surface Height", config.sidewalk_surface_height, 0.005f, 0.001f, 8.0f, "%.3f");
        edit_float("Sidewalk Surface Lift", config.sidewalk_surface_lift, 0.001f, 0.0f, 4.0f, "%.3f");
        edit_float("World Floor Height Scale", config.world_floor_height_scale, 0.01f, 0.0f, 4.0f, "%.2f");
        edit_float("World Floor Top Y", config.world_floor_top_y, 0.001f, -8.0f, 8.0f, "%.3f");
        edit_float("World Floor Grid Y Offset", config.world_floor_grid_y_offset, 0.001f, 0.0f, 4.0f, "%.3f");
        edit_float("World Floor Grid Tile Scale", config.world_floor_grid_tile_scale, 0.05f, 0.1f, 64.0f, "%.2f");
        edit_float("World Floor Grid Line Width", config.world_floor_grid_line_width, 0.005f, 0.001f, 4.0f, "%.3f");
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("##renderer_lighting", ImGuiTreeNodeFlags_SpanAvailWidth, "Lighting"))
    {
        edit_float("Ambient", config.ambient_strength, 0.01f, 0.0f, 4.0f, "%.2f");
        edit_float("Directional Light X", config.directional_light_x, 0.01f, -4.0f, 4.0f, "%.2f");
        edit_float("Directional Light Y", config.directional_light_y, 0.01f, -4.0f, 4.0f, "%.2f");
        edit_float("Directional Light Z", config.directional_light_z, 0.01f, -4.0f, 4.0f, "%.2f");
        const std::array<float, 4> previous_point_light = {
            config.point_light_x,
            config.point_light_y,
            config.point_light_z,
            config.point_light_radius,
        };
        edit_float("Point Light X", config.point_light_x, 0.05f, -1024.0f, 1024.0f, "%.2f");
        edit_float("Point Light Y", config.point_light_y, 0.05f, -1024.0f, 1024.0f, "%.2f");
        edit_float("Point Light Z", config.point_light_z, 0.05f, -1024.0f, 1024.0f, "%.2f");
        edit_float("Point Light Radius", config.point_light_radius, 0.05f, 0.1f, 2048.0f, "%.2f");
        if (previous_point_light
            != std::array<float, 4>{
                config.point_light_x,
                config.point_light_y,
                config.point_light_z,
                config.point_light_radius,
            })
        {
            config.point_light_position_valid = true;
        }
        edit_float("Point Light Brightness", config.point_light_brightness, 0.01f, 0.0f, 8.0f, "%.2f");
        edit_float("Output Gamma", config.output_gamma, 0.01f, 0.1f, 4.0f, "%.2f");
        ImGui::TreePop();
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.68f, 0.68f, 0.68f, 1.0f));
    ImGui::TextUnformatted("Fade uses projected ink size on screen, not camera distance.");
    ImGui::TextUnformatted("Gamma is a Megacity-only final output curve, not true sRGB backbuffer conversion.");
    ImGui::TextUnformatted("Point light position is in absolute world space.");
    ImGui::PopStyleColor();

    controls.config = config;

    ImGui::TreePop();
    return changed || controls.rebuild_requested || controls.reset_camera_requested
        || controls.set_defaults_requested || controls.committed_edit;
}

} // namespace

// ---- Public entry point --------------------------------------------------------

bool render_treesitter_panel(
    int window_w,
    int window_h,
    const std::shared_ptr<const CodebaseSnapshot>& snapshot,
    const SemanticMegacityModel* semantic_model,
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

    const SnapshotUiCache& ui_cache = cached_snapshot_ui(snapshot);

    ImGui::Separator();
    render_stats(ui_cache.stats);
    ImGui::Separator();

    ImGui::BeginChild("##content", ImVec2(0.0f, 0.0f), false,
        ImGuiWindowFlags_HorizontalScrollbar);

    if (renderer_controls)
        changed |= render_renderer_controls(*renderer_controls);

    // ---- Files root ------------------------------------------------------------
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
    const bool files_open = ImGui::TreeNodeEx(
        "##files_root", ImGuiTreeNodeFlags_SpanAvailWidth,
        "Files (%zu)", snapshot->files.size());
    ImGui::PopStyleColor();
    if (files_open)
    {
        render_files_tree(ui_cache.files);
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
        render_objects_tree(ui_cache.objects);
        ImGui::TreePop();
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
    const bool city_open = ImGui::TreeNodeEx(
        "##city_preview_root", ImGuiTreeNodeFlags_SpanAvailWidth,
        "City Preview");
    ImGui::PopStyleColor();
    if (city_open)
    {
        render_city_preview(semantic_model);
        ImGui::TreePop();
    }

    ImGui::EndChild();

    ImGui::PopStyleVar(2);
    ImGui::End();
    return changed;
}

} // namespace draxul

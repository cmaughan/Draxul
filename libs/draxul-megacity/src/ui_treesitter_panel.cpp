#include "ui_treesitter_panel.h"

#include <imgui.h>

#include <algorithm>
#include <string_view>
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
    std::string_view file; // points into snapshot string; valid for snapshot lifetime
    uint32_t line;
    bool is_abstract;
    SymbolKind kind; // Class or Struct
};

struct FuncEntry
{
    std::string name;
    std::string_view file;
    uint32_t line;
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
    // Collect across all files
    std::vector<ClassEntry> concrete, abstract;
    std::vector<FuncEntry> free_functions;

    for (const auto& file : snap.files)
    {
        const std::string_view basename = path_basename(file.path);
        for (const auto& sym : file.symbols)
        {
            if (sym.kind == SymbolKind::Class || sym.kind == SymbolKind::Struct)
            {
                ClassEntry e{ sym.name, basename, sym.line, sym.is_abstract, sym.kind };
                if (sym.is_abstract)
                    abstract.push_back(std::move(e));
                else
                    concrete.push_back(std::move(e));
            }
            else if (sym.kind == SymbolKind::Function && sym.parent.empty())
            {
                free_functions.push_back({ sym.name, basename, sym.line });
            }
        }
    }

    auto by_name_then_file = [](const auto& a, const auto& b) {
        if (a.name != b.name)
            return a.name < b.name;
        return a.file < b.file;
    };
    std::sort(concrete.begin(), concrete.end(), by_name_then_file);
    std::sort(abstract.begin(), abstract.end(), by_name_then_file);
    std::sort(free_functions.begin(), free_functions.end(), by_name_then_file);

    // ---- Classes ----
    const bool classes_open = ImGui::TreeNodeEx(
        "##classes", ImGuiTreeNodeFlags_SpanAvailWidth,
        "Classes (%zu concrete, %zu abstract)",
        concrete.size(), abstract.size());
    if (classes_open)
    {
        // Abstract sub-tree
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.65f, 0.40f, 1.0f));
        const bool abs_open = ImGui::TreeNodeEx(
            "##abstract", ImGuiTreeNodeFlags_SpanAvailWidth,
            "Abstract (%zu)", abstract.size());
        ImGui::PopStyleColor();
        if (abs_open)
        {
            for (const auto& e : abstract)
            {
                render_symbol_leaf(
                    (e.name + std::string(e.file)).c_str(),
                    symbol_kind_icon(e.kind),
                    symbol_kind_color(e.kind),
                    e.name, e.file, e.line);
            }
            ImGui::TreePop();
        }

        // Concrete sub-tree
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
        const bool con_open = ImGui::TreeNodeEx(
            "##concrete", ImGuiTreeNodeFlags_SpanAvailWidth,
            "Concrete (%zu)", concrete.size());
        ImGui::PopStyleColor();
        if (con_open)
        {
            for (const auto& e : concrete)
            {
                render_symbol_leaf(
                    (e.name + std::string(e.file)).c_str(),
                    symbol_kind_icon(e.kind),
                    symbol_kind_color(e.kind),
                    e.name, e.file, e.line);
            }
            ImGui::TreePop();
        }

        ImGui::TreePop();
    }

    // ---- Free functions ----
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.85f, 1.00f, 1.0f));
    const bool fn_open = ImGui::TreeNodeEx(
        "##functions", ImGuiTreeNodeFlags_SpanAvailWidth,
        "Functions (%zu)", free_functions.size());
    ImGui::PopStyleColor();
    if (fn_open)
    {
        for (const auto& e : free_functions)
        {
            render_symbol_leaf(
                (e.name + std::string(e.file)).c_str(),
                "fn ",
                { 0.60f, 0.85f, 1.00f, 1.0f },
                e.name, e.file, e.line);
        }
        ImGui::TreePop();
    }
}

} // namespace

// ---- Public entry point --------------------------------------------------------

void render_treesitter_panel(
    int window_w,
    int window_h,
    const std::shared_ptr<const CodebaseSnapshot>& snapshot)
{
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(
        ImVec2(static_cast<float>(window_w) * 0.5f, static_cast<float>(window_h)),
        ImGuiCond_FirstUseEver);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 3.0f));

    if (!ImGui::Begin("Codebase Analysis", nullptr, flags))
    {
        ImGui::PopStyleVar(2);
        ImGui::End();
        return;
    }

    if (!snapshot)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        ImGui::Text("(starting...)");
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        ImGui::End();
        return;
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

    ImGui::EndChild();

    ImGui::PopStyleVar(2);
    ImGui::End();
}

} // namespace draxul

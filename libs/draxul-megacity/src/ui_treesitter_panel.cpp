#include "ui_treesitter_panel.h"

#include <imgui.h>

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
        err_count += f.error_count;
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

} // namespace

void render_treesitter_panel(
    int window_w,
    int window_h,
    const std::shared_ptr<const CodebaseSnapshot>& snapshot)
{
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(
        ImVec2(static_cast<float>(window_w), static_cast<float>(window_h)));
    ImGui::SetNextWindowBgAlpha(0.88f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 3.0f));

    if (!ImGui::Begin("##treesitter", nullptr, flags))
    {
        ImGui::PopStyleVar(2);
        ImGui::End();
        return;
    }

    // Title bar
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.4f, 1.0f));
    ImGui::Text("Codebase Analysis");
    ImGui::PopStyleColor();
    ImGui::SameLine();

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
        ImGui::Text("(scanning... %zu files)", snapshot->files.size());
        ImGui::PopStyleColor();
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        ImGui::Text("(complete)");
        ImGui::PopStyleColor();
    }

    ImGui::Separator();
    render_stats(*snapshot);
    ImGui::Separator();

    // File tree
    ImGui::BeginChild("##filetree", ImVec2(0.0f, 0.0f), false,
        ImGuiWindowFlags_HorizontalScrollbar);

    for (const auto& file : snapshot->files)
    {
        const ImGuiTreeNodeFlags file_flags = ImGuiTreeNodeFlags_SpanAvailWidth
            | (file.symbols.empty() ? ImGuiTreeNodeFlags_Leaf : 0);

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
        const bool open = ImGui::TreeNodeEx(
            file.path.c_str(), file_flags, "%s", file.path.c_str());
        ImGui::PopStyleColor();

        if (open)
        {
            for (const auto& sym : file.symbols)
            {
                if (sym.kind == SymbolKind::Include)
                    continue; // skip includes in the tree to reduce noise

                ImGui::PushStyleColor(
                    ImGuiCol_Text, symbol_kind_color(sym.kind));
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

    ImGui::EndChild();

    ImGui::PopStyleVar(2);
    ImGui::End();
}

} // namespace draxul

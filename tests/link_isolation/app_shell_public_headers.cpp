#include <draxul/app_shell_layout.h>
#include <draxul/chrome_layout.h>
#include <draxul/fuzzy_match.h>
#include <draxul/rename_editor.h>
#include <draxul/split_tree.h>

int main()
{
    const auto shell = draxul::compute_app_shell_layout({
        .window_width = 800,
        .window_height = 600,
        .cell_width = 8,
        .cell_height = 16,
    });
    draxul::SplitTree tree;
    tree.reset(shell.pane_root.w, shell.pane_root.h);
    return draxul::fuzzy_match("tab", "new_tab").matched ? 0 : 1;
}

#include "support/test_terminal_host_fixture.h"
#include "support/test_vt_terminal_host.h"

#include <catch2/catch_all.hpp>
#include <draxul/terminal_identity.h>
#include <draxul/terminal_snapshot.h>

#include <string>
#include <string_view>

using namespace draxul;
using namespace draxul::tests;

namespace
{

constexpr std::string_view kSemanticReplay
    = "\x1B]0;semantic replay\x07"
      "\x1B]7;file://localhost/D:/dev/Draxul%20Tree\x07"
      "\x1B[1;31mred\x1B[0m "
      "\xE7\x95\x8C"
      "\x1B]8;;https://example.test/path\x1B\\link\x1B]8;;\x1B\\"
      "\r\n"
      "\x1B]133;A\x1B\\prompt"
      "\x1B]133;B\x1B\\"
      "\x1B]133;C\x1B\\output"
      "\x1B]133;D;7\x1B\\"
      "\x1B[?1h\x1B[?2004h\x1B[?1000h\x1B[?1002h\x1B[?1006h"
      "\x1B[3;5H";

TerminalSemanticSnapshot replay_in_chunks(std::string_view bytes, size_t chunk_size)
{
    TerminalHostFixture<TestVtTerminalHost> setup(24, 6);
    REQUIRE(setup.ok);
    for (size_t offset = 0; offset < bytes.size(); offset += chunk_size)
        setup.host.feed(bytes.substr(offset, std::min(chunk_size, bytes.size() - offset)));
    return setup.host.snapshot();
}

} // namespace

TEST_CASE("terminal semantic replay is independent of PTY chunking",
    "[terminal][semantic-snapshot][replay]")
{
    const auto whole = replay_in_chunks(kSemanticReplay, kSemanticReplay.size());
    const auto bytewise = replay_in_chunks(kSemanticReplay, 1);
    const auto uneven = replay_in_chunks(kSemanticReplay, 7);

    REQUIRE(whole == bytewise);
    REQUIRE(whole == uneven);
    CHECK(terminal_semantic_digest(whole) == 184916965083866202ULL);
}

TEST_CASE("terminal semantic snapshot captures renderer-neutral state",
    "[terminal][semantic-snapshot]")
{
    TerminalHostFixture<TestVtTerminalHost> setup(24, 6);
    REQUIRE(setup.ok);
    setup.host.feed(kSemanticReplay);
    const TerminalSemanticSnapshot snapshot = setup.host.snapshot();

    CHECK(snapshot.cols == 24);
    CHECK(snapshot.rows == 6);
    CHECK(snapshot.cells.size() == 144);
    CHECK(snapshot.metadata.title == "semantic replay");
    CHECK(snapshot.metadata.working_directory == "/D:/dev/Draxul Tree");
    CHECK(snapshot.metadata.cursor.col == 4);
    CHECK(snapshot.metadata.cursor.row == 2);
    CHECK(snapshot.metadata.modes.cursor_application);
    CHECK(snapshot.metadata.modes.bracketed_paste);
    CHECK(snapshot.metadata.modes.mouse.normal_tracking);
    CHECK(snapshot.metadata.modes.mouse.button_motion);
    CHECK(snapshot.metadata.modes.mouse.sgr_coordinates);
    REQUIRE(snapshot.metadata.shell_marks.size() == 4);
    CHECK(snapshot.metadata.shell_marks.back().kind == TerminalShellMarkKind::OutputEnd);
    CHECK(snapshot.metadata.shell_marks.back().exit_code == 7);

    bool saw_link = false;
    bool saw_wide = false;
    for (const auto& cell : snapshot.cells)
    {
        saw_link = saw_link || cell.hyperlink == "https://example.test/path";
        saw_wide = saw_wide || cell.double_width;
    }
    CHECK(saw_link);
    CHECK(saw_wide);
}

TEST_CASE("terminal semantic replay captures alternate screen and resize",
    "[terminal][semantic-snapshot][resize]")
{
    TerminalHostFixture<TestVtTerminalHost> setup(8, 3);
    REQUIRE(setup.ok);
    setup.host.feed("main\x1B[?1049halt");

    HostViewport resized;
    resized.grid_size = { 10, 4 };
    setup.host.set_viewport(resized);

    const auto alternate = setup.host.snapshot();
    CHECK(alternate.cols == 10);
    CHECK(alternate.rows == 4);
    CHECK(alternate.metadata.modes.alternate_screen);
    CHECK(terminal_semantic_digest(alternate) == 14064452814120280018ULL);

    setup.host.feed("\x1B[?1049l");
    const auto restored = setup.host.snapshot();
    CHECK_FALSE(restored.metadata.modes.alternate_screen);
    CHECK(restored.cells.front().text == "m");
    CHECK(terminal_semantic_digest(restored) == 7904877978670213116ULL);
}

TEST_CASE("terminal semantic replay captures OSC 52 clipboard behavior",
    "[terminal][semantic-snapshot][clipboard]")
{
    TerminalHostFixture<TestVtTerminalHost> setup(8, 3);
    REQUIRE(setup.ok);
    setup.host.feed("\x1B]52;c;Y2xpcGJvYXJk\x1B\\");
    CHECK(setup.window.clipboard_text() == "clipboard");
}

TEST_CASE("terminal identity values distinguish runtime generations",
    "[terminal][identity]")
{
    constexpr TerminalId terminal{ 41 };
    constexpr TerminalRuntimeGeneration first{ 1 };
    constexpr TerminalRuntimeGeneration second{ 2 };
    constexpr TerminalSequence initial{ 0 };

    STATIC_CHECK(terminal.valid());
    STATIC_CHECK(first.valid());
    STATIC_CHECK(first != second);
    STATIC_CHECK(initial == TerminalSequence{});
    STATIC_CHECK(TerminalStateLimits::kMaxCells
        == static_cast<size_t>(TerminalStateLimits::kMaxColumns)
            * TerminalStateLimits::kMaxRows);
}

#include <catch2/catch_all.hpp>
#include <draxul/process_util.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace
{

std::string read_project_file(std::string_view relative_path)
{
    const auto path = std::filesystem::path(DRAXUL_PROJECT_ROOT) / relative_path;
    std::ifstream in(path);
    REQUIRE(in.good());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string between_markers(
    const std::string& text, std::string_view begin_marker, std::string_view end_marker)
{
    const size_t begin = text.find(begin_marker);
    REQUIRE(begin != std::string::npos);
    const size_t end = text.find(end_marker, begin + begin_marker.size());
    REQUIRE(end != std::string::npos);
    return text.substr(begin, end - begin);
}

} // namespace

TEST_CASE("quote_windows_arg applies CreateProcess-compatible quoting", "[process_util]")
{
    using draxul::quote_windows_arg;

    CHECK(quote_windows_arg("nvim") == "nvim");
    CHECK(quote_windows_arg("") == "\"\"");
    CHECK(quote_windows_arg("C:\\Program Files\\nvim\\nvim.exe")
        == "\"C:\\Program Files\\nvim\\nvim.exe\"");
    CHECK(quote_windows_arg("say \"hello\"")
        == "\"say \\\"hello\\\"\"");
    CHECK(quote_windows_arg("C:\\Tools\\") == "C:\\Tools\\");
    CHECK(quote_windows_arg("C:\\Program Files\\")
        == "\"C:\\Program Files\\\\\"");
    CHECK(quote_windows_arg("C:\\path\\\\")
        == "C:\\path\\\\");
}

TEST_CASE("Unix fork child paths do not allocate or mutate process environment", "[process_util]")
{
    const std::string unix_pty
        = read_project_file("libs/draxul-terminal-process/src/unix_pty_process.cpp");
    const std::string unix_child = between_markers(
        unix_pty, "if (pid_ == 0)", "// Parent process.");
    CHECK(unix_child.find("std::string") == std::string::npos);
    CHECK(unix_child.find("std::vector") == std::string::npos);
    CHECK(unix_child.find("setenv") == std::string::npos);
    CHECK(unix_child.find("putenv") == std::string::npos);

    const std::string nvim
        = read_project_file("libs/draxul-nvim/src/nvim_process.cpp");
    const std::string nvim_child = between_markers(
        nvim,
        "if (pid == 0)",
        "    close(stdin_pipe[0]);\n    close(stdout_pipe[1]);\n    close(exec_status_pipe[1]);");
    CHECK(nvim_child.find("std::string") == std::string::npos);
    CHECK(nvim_child.find("std::vector") == std::string::npos);
    CHECK(nvim_child.find("setenv") == std::string::npos);
    CHECK(nvim_child.find("putenv") == std::string::npos);
}

TEST_CASE("UnixPtyProcess shutdown joins the reader before detached child reaping", "[process_util]")
{
    const std::string unix_pty
        = read_project_file("libs/draxul-terminal-process/src/unix_pty_process.cpp");
    const std::string shutdown = between_markers(
        unix_pty, "void UnixPtyProcess::shutdown()", "void UnixPtyProcess::request_close()");

    const size_t join_pos = shutdown.find("reader_copy.join()");
    REQUIRE(join_pos != std::string::npos);

    const size_t detach_pos = shutdown.find(".detach()");
    REQUIRE(detach_pos != std::string::npos);
    CHECK(join_pos < detach_pos);
}

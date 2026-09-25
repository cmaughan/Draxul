#include "support/scoped_env_var.h"
#include "support/temp_dir.h"
#include "support/test_support.h"

#include <draxul/log.h>
#include <draxul/nvim_transport.h>
#include <draxul/nvim_ui.h>

#include <SDL3/SDL.h>

#include <atomic>
#include <array>
#include <catch2/catch_all.hpp>
#include <chrono>
#include <filesystem>
#include <latch>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
// clang-format off
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// clang-format on
#endif

using namespace draxul;
using namespace draxul::tests;

namespace
{

std::string missing_nvim_path()
{
#ifdef _WIN32
    return "C:\\definitely-missing\\draxul-test-nvim.exe";
#else
    return "/definitely-missing/draxul-test-nvim";
#endif
}

bool contains_spawn_error(const std::vector<LogRecord>& records)
{
    for (const auto& record : records)
    {
        if (record.category == LogCategory::Nvim
            && record.level == LogLevel::Error
            && record.message.find("Failed to spawn nvim") != std::string::npos)
            return true;
    }
    return false;
}

#ifdef _WIN32
std::string path_utf8(const std::filesystem::path& path)
{
    const auto value = path.u8string();
    return { reinterpret_cast<const char*>(value.data()), value.size() };
}

class ScopedWideEnvVar
{
public:
    ScopedWideEnvVar(const wchar_t* name, const wchar_t* value)
        : name_(name)
    {
        DWORD required = GetEnvironmentVariableW(name_, nullptr, 0);
        if (required != 0)
        {
            previous_.resize(required);
            GetEnvironmentVariableW(name_, previous_.data(), required);
            previous_.resize(required - 1);
            had_previous_ = true;
        }
        SetEnvironmentVariableW(name_, value);
    }

    ~ScopedWideEnvVar()
    {
        SetEnvironmentVariableW(name_, had_previous_ ? previous_.c_str() : nullptr);
    }

private:
    const wchar_t* name_;
    std::wstring previous_;
    bool had_previous_ = false;
};
#endif

} // namespace

TEST_CASE("nvim process spawn returns false and logs an error for a bad path", "[nvim]")
{
    ScopedLogCapture capture;
    NvimProcess process;

    INFO("spawn should fail for a missing nvim binary");
    REQUIRE(!process.spawn(missing_nvim_path()));
    INFO("failed spawn should not leave a running process");
    REQUIRE(!process.is_running());
    INFO("failed spawn should emit an error log");
    REQUIRE(contains_spawn_error(capture.records));
}

TEST_CASE("nvim process shutdown is a no-op after spawn failure", "[nvim]")
{
    NvimProcess process;

    INFO("spawn should fail for a missing nvim binary");
    REQUIRE(!process.spawn(missing_nvim_path()));
    process.shutdown();
    INFO("shutdown after failed spawn should remain safe");
    REQUIRE(!process.is_running());
}

#ifdef _WIN32
TEST_CASE("Windows nvim launch preserves Unicode paths arguments and environment", "[nvim][windows]")
{
    INFO("Windows ANSI code page: " << GetACP());
    tests::TempDir temp("draxul-nvim-unicode-launch");
    const auto executable_dir = temp.path / L"\u65e5\u672c\u8a9e exe";
    const auto working_dir = temp.path / L"\u03b1\u03b2 work";
    std::filesystem::create_directories(executable_dir);
    std::filesystem::create_directories(working_dir);
    const auto executable = executable_dir / L"rpc \u00e9.exe";
    std::filesystem::copy_file(DRAXUL_RPC_FAKE_PATH, executable);
    const auto dump_path = temp.path / "launch.txt";
    ScopedEnvVar mode("DRAXUL_RPC_FAKE_MODE", "dump_launch_and_exit");
    ScopedEnvVar dump("DRAXUL_RPC_FAKE_LAUNCH_DUMP", dump_path.string().c_str());
    ScopedWideEnvVar unicode(L"DRAXUL_RPC_FAKE_UNICODE", L"\u6f22\u5b57");

    const std::string argument = "\xCE\xB2 eta \"quoted\" \\";
    NvimProcess process;
    const auto result = process.spawn(path_utf8(executable), { argument }, path_utf8(working_dir));
    REQUIRE(result);
    if (result)
    {
        const bool exited = wait_until([&] { return !process.is_running(); },
            std::chrono::seconds(3));
        process.shutdown();
        REQUIRE(exited);
        const std::string output = read_file(dump_path);
        CHECK(output.find("cwd=" + path_utf8(working_dir) + "\n") != std::string::npos);
        CHECK(output.find("arg0=" + path_utf8(executable) + "\n") != std::string::npos);
        CHECK(output.find("arg1=--embed\n") != std::string::npos);
        CHECK(output.find("arg2=" + argument + "\n") != std::string::npos);
        CHECK(output.find("env=\xE6\xBC\xA2\xE5\xAD\x97\n") != std::string::npos);
    }
}

#endif

TEST_CASE("paired printable events reach a real Neovim buffer exactly once",
    "[nvim][input][integration]")
{
    // Destruction closes the child pipe before joining the RPC reader even if
    // an assertion below fails.
    NvimRpc rpc;
    NvimProcess process;
    REQUIRE(process.spawn("nvim", { "-u", "NONE", "--noplugin", "-n" }));
    REQUIRE(rpc.initialize(process));

    REQUIRE(rpc.request("nvim_ui_attach", {
        MpackValue::make_int(80), MpackValue::make_int(24),
        MpackValue::make_map({
            { MpackValue::make_str("rgb"), MpackValue::make_bool(true) },
            { MpackValue::make_str("ext_linegrid"), MpackValue::make_bool(true) },
        }),
    }).has_value());

    const auto command = [&](const char* value) {
        return rpc.request("nvim_command", { MpackValue::make_str(value) });
    };
    REQUIRE(command("inoremap <C-Space> Q").has_value());
    REQUIRE(command("inoremap <M-Bslash> R").has_value());
    REQUIRE(rpc.request("nvim_input", { MpackValue::make_str("i") }).has_value());
    std::string last_mode;
    const bool entered_insert = wait_until([&] {
        const auto mode = rpc.request("nvim_get_mode", {});
        if (!mode || mode.value().type() != MpackValue::Map)
            return false;
        const auto& fields = mode.value().as_map();
        for (const auto& [key, value] : fields)
        {
            if (key.type() == MpackValue::String && key.as_str() == "mode"
                && value.type() == MpackValue::String)
            {
                last_mode = value.as_str();
                return last_mode == "i";
            }
        }
        return false;
    }, std::chrono::seconds(2));
    INFO("last Neovim mode: " << last_mode);
    REQUIRE(entered_insert);

    NvimInput input;
    input.initialize(&rpc, 8, 16);
    const auto type = [&](int keycode, ModifierFlags modifiers, const char* text) {
        input.on_key({ 0, keycode, modifiers, true });
        input.on_text_input({ text });
    };
    type(SDLK_SPACE, kModNone, " ");
    type(SDLK_BACKSLASH, kModNone, "\\");
    type(SDLK_BACKSLASH, kModShift, "|");
    type(SDLK_LESS, kModNone, "<");
    type(SDLK_SPACE, kModCtrl, " ");
    type(SDLK_BACKSLASH, kModAlt, "\\");

    const auto expected = std::string(" \\|<QR");
    REQUIRE(wait_until([&] {
        const auto line = rpc.request("nvim_get_current_line", {});
        return line && line.value().type() == MpackValue::String
            && line.value().as_str() == expected;
    }, std::chrono::seconds(2)));
    process.shutdown();
    rpc.shutdown();
}

#ifdef _WIN32

TEST_CASE("Windows launches real Neovim from Unicode executable and working directories", "[nvim][windows][integration]")
{
    INFO("Windows ANSI code page: " << GetACP());
    std::array<wchar_t, MAX_PATH> resolved{};
    const DWORD length = SearchPathW(nullptr, L"nvim.exe", nullptr,
        static_cast<DWORD>(resolved.size()), resolved.data(), nullptr);
    if (length == 0 || length >= resolved.size())
        SKIP("nvim.exe is unavailable on PATH for real-process integration");

    tests::TempDir temp("draxul-real-nvim-unicode-launch");
    const auto executable_dir = temp.path / L"\u65e5\u672c\u8a9e exe";
    const auto working_dir = temp.path / L"\u03b1\u03b2 work";
    std::filesystem::create_directories(executable_dir);
    std::filesystem::create_directories(working_dir);
    const auto source = std::filesystem::path(resolved.data());
    const auto executable = executable_dir / L"nvim \u00e9.exe";
    std::filesystem::copy_file(source, executable);
    for (const auto& sibling : std::filesystem::directory_iterator(source.parent_path()))
    {
        if (sibling.path().extension() == L".dll")
            std::filesystem::copy_file(sibling.path(), executable_dir / sibling.path().filename());
    }

    NvimProcess process;
    REQUIRE(process.spawn(path_utf8(executable), {}, path_utf8(working_dir)));
    NvimRpc rpc;
    REQUIRE(rpc.initialize(process));
    const auto cwd = rpc.request("nvim_call_function", {
        MpackValue::make_str("getcwd"), MpackValue::make_array({}) });
    REQUIRE(cwd.has_value());
    REQUIRE(cwd.value().type() == MpackValue::String);
    CHECK(std::filesystem::equivalent(
        std::filesystem::u8path(cwd.value().as_str()), working_dir));
    process.shutdown();
    rpc.shutdown();
    CHECK(wait_until([&] {
        std::error_code error;
        std::filesystem::remove_all(executable_dir, error);
        return !error && !std::filesystem::exists(executable_dir);
    }, std::chrono::seconds(5)));
}

TEST_CASE("Windows nvim process status is safe during concurrent shutdown", "[nvim][windows]")
{
    ScopedEnvVar mode("DRAXUL_RPC_FAKE_MODE", "hang");
    NvimProcess process;

    // Warm up CreateProcess and the fake RPC helper before taking the process
    // handle baseline. This keeps one-time loader/runtime handles out of the
    // ownership check below.
    REQUIRE(process.spawn(DRAXUL_RPC_FAKE_PATH));
    process.shutdown();
    DWORD handle_count_before = 0;
    REQUIRE(GetProcessHandleCount(GetCurrentProcess(), &handle_count_before));

    // Reusing one process wrapper also verifies that shutdown clears all
    // published process identity before the next spawn.
    for (int cycle = 0; cycle < 16; ++cycle)
    {
        INFO("cycle " << cycle);
        REQUIRE(process.spawn(DRAXUL_RPC_FAKE_PATH));
        REQUIRE(process.is_running());

        std::latch start{ 2 };
        std::latch first_status_check{ 1 };
        std::atomic<bool> shutdown_complete{ false };
        std::atomic<bool> post_shutdown_all_stopped{ true };
        std::atomic<size_t> status_checks{ 0 };
        std::thread observer([&]() {
            start.count_down();
            start.wait();
            static_cast<void>(process.is_running());
            status_checks.fetch_add(1, std::memory_order_relaxed);
            first_status_check.count_down();
            while (!shutdown_complete.load(std::memory_order_acquire))
            {
                static_cast<void>(process.is_running());
                status_checks.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::yield();
            }

            // Keep querying after the handle has been unpublished and closed.
            for (int i = 0; i < 256; ++i)
            {
                if (process.is_running())
                    post_shutdown_all_stopped.store(false, std::memory_order_relaxed);
            }
        });

        start.count_down();
        start.wait();
        first_status_check.wait();
        process.shutdown();
        shutdown_complete.store(true, std::memory_order_release);
        observer.join();

        REQUIRE(status_checks.load(std::memory_order_relaxed) > 0);
        REQUIRE(post_shutdown_all_stopped.load(std::memory_order_relaxed));
        REQUIRE_FALSE(process.is_running());
        process.shutdown(); // Repeated shutdown must remain a no-op.
    }

    DWORD handle_count_after = 0;
    const bool reaped = wait_until(
        [&] {
            return GetProcessHandleCount(GetCurrentProcess(), &handle_count_after)
                && handle_count_after <= handle_count_before + 2;
        },
        std::chrono::seconds(5));
    INFO("process handles before=" << handle_count_before << " after=" << handle_count_after);
    // Allow a very small amount of unrelated test/runtime noise while still
    // making a leaked process/thread/pipe set fail deterministically.
    CHECK(reaped);
}

TEST_CASE("Windows nvim process reports an already-exited child as stopped", "[nvim][windows]")
{
    ScopedEnvVar mode("DRAXUL_RPC_FAKE_MODE", "dump_term_and_exit");
    NvimProcess process;
    REQUIRE(process.spawn(DRAXUL_RPC_FAKE_PATH));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (process.is_running() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();

    REQUIRE_FALSE(process.is_running());
    process.shutdown();
    REQUIRE_FALSE(process.is_running());
}

TEST_CASE("Windows nvim shutdown reaps an unresponsive child off the caller thread", "[nvim][windows][shutdown]")
{
    DWORD handle_count_before = 0;
    REQUIRE(GetProcessHandleCount(GetCurrentProcess(), &handle_count_before));

    ScopedEnvVar mode("DRAXUL_RPC_FAKE_MODE", "unresponsive");
    NvimProcess process;
    REQUIRE(process.spawn(DRAXUL_RPC_FAKE_PATH));

    const auto started = std::chrono::steady_clock::now();
    process.shutdown();
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(250));
    CHECK_FALSE(process.is_running());

    DWORD handle_count_after = 0;
    const bool reaped = wait_until(
        [&] {
            return GetProcessHandleCount(GetCurrentProcess(), &handle_count_after)
                && handle_count_after <= handle_count_before + 1;
        },
        std::chrono::seconds(5));
    INFO("process handles before=" << handle_count_before << " after=" << handle_count_after);
    CHECK(reaped);
}
#endif

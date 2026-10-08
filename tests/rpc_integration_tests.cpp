#include "support/scoped_env_var.h"
#include "support/temp_dir.h"
#include "support/test_support.h"

#include <atomic>
#include <catch2/catch_all.hpp>
#include <chrono>
#include <draxul/log.h>
#include <draxul/nvim_transport.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <utility>

using namespace draxul;
using namespace draxul::tests;

namespace
{

std::string helper_path()
{
    return DRAXUL_RPC_FAKE_PATH;
}

bool has_log_message(const std::vector<LogRecord>& records, LogLevel level, LogCategory category, std::string_view needle)
{
    for (const auto& record : records)
    {
        if (record.level == level && record.category == category && record.message.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

RpcResult run_request_with_mode(const char* mode, std::vector<RpcNotification>* notifications = nullptr)
{
    ScopedEnvVar env("DRAXUL_RPC_FAKE_MODE", mode);
    NvimProcess process;
    INFO("fake RPC server spawns");
    REQUIRE(process.spawn(helper_path()));

    NvimRpc rpc;
    INFO("rpc initializes");
    REQUIRE(rpc.initialize(process));

    RpcResult result = rpc.request("fake_method", { MpackValue::make_int(7) });
    if (notifications)
        *notifications = rpc.drain_notifications();

    rpc.shutdown();
    process.shutdown();
    return result;
}

} // namespace

TEST_CASE("nvim rpc returns successful responses from the transport", "[rpc]")
{
    RpcResult result = run_request_with_mode("success");

    INFO("request reports overall success");
    REQUIRE(result.has_value());
    INFO("request result payload survives");
    REQUIRE(result.value().as_str() == std::string("ok"));
}

TEST_CASE("nvim rpc surfaces remote errors without losing transport success", "[rpc]")
{
    RpcResult result = run_request_with_mode("error");

    INFO("remote error makes the request unsuccessful");
    REQUIRE(!result.has_value());
    INFO("remote error is classified as RpcError, not a transport failure");
    REQUIRE(result.error().kind == ErrorKind::RpcError);
    INFO("remote error payload survives in the message");
    REQUIRE(result.error().message == std::string("boom"));
}

TEST_CASE("nvim rpc aborts cleanly when the child exits without responding", "[rpc]")
{
    auto start = std::chrono::steady_clock::now();
    RpcResult result = run_request_with_mode("abort_after_read");
    auto elapsed = std::chrono::steady_clock::now() - start;

    INFO("aborted transport reports failure");
    REQUIRE(!result.has_value());
    INFO("aborted transport is classified as IoError");
    REQUIRE(result.error().kind == ErrorKind::IoError);
    INFO("aborted transport returns promptly without timing out");
    REQUIRE(elapsed < std::chrono::seconds(2));
}

TEST_CASE("nvim rpc queues notifications received before the response", "[rpc]")
{
    std::vector<RpcNotification> notifications;
    RpcResult result = run_request_with_mode("notify_then_success", &notifications);

    INFO("request still succeeds when a notification precedes the response");
    REQUIRE(result.has_value());
    INFO("notification is queued");
    REQUIRE(static_cast<int>(notifications.size()) == 1);
    INFO("notification method survives");
    REQUIRE(notifications[0].method == std::string("redraw"));
    INFO("notification params survive");
    REQUIRE(static_cast<int>(notifications[0].params.size()) == 1);
    INFO("notification param payload survives");
    REQUIRE(notifications[0].params[0].type() == MpackValue::Array);
}

TEST_CASE("nvim rpc treats malformed responses as transport failure", "[rpc]")
{
    auto start = std::chrono::steady_clock::now();
    RpcResult result = run_request_with_mode("malformed_response");
    auto elapsed = std::chrono::steady_clock::now() - start;

    INFO("malformed response reports transport failure");
    REQUIRE(!result.has_value());
    REQUIRE(result.error().kind == ErrorKind::IoError);
    INFO("malformed response returns promptly without timing out");
    REQUIRE(elapsed < std::chrono::seconds(2));
}

TEST_CASE("nvim rpc reader survives a type-mismatched RPC packet and keeps delivering valid traffic", "[rpc]")
{
    ScopedLogCapture capture;

    // WI 05: the fake server sends a structurally valid msgpack array whose
    // fields have the wrong types (string where an int is expected), then a
    // real success response. Before the fix, the first packet would throw
    // std::bad_variant_access out of the reader thread and terminate the
    // process; the second packet would never be seen. After the fix, the
    // reader logs + counts the malformed packet and continues to dispatch.
    RpcResult result = run_request_with_mode("malformed_dispatch_then_success");

    INFO("reader still delivers the subsequent valid response");
    REQUIRE(result.has_value());
    REQUIRE(result.value().as_str() == std::string("ok"));
    INFO("reader logs an error with a hex dump for the malformed packet");
    REQUIRE(has_log_message(capture.records, LogLevel::Error, LogCategory::Rpc, "Malformed RPC packet"));
}

TEST_CASE("nvim rpc close() unblocks an in-flight request without waiting for timeout", "[rpc]")
{
    ScopedEnvVar env("DRAXUL_RPC_FAKE_MODE", "hang");
    NvimProcess process;
    INFO("fake RPC server spawns");
    REQUIRE(process.spawn(helper_path()));

    NvimRpc rpc;
    INFO("rpc initializes");
    REQUIRE(rpc.initialize(process));

    // Send a request that will block forever (server never responds in hang mode).
    std::thread requester([&rpc]() {
        rpc.request("fake_method", { MpackValue::make_int(7) });
    });

    // Give the request time to send and block on the response CV.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto start = std::chrono::steady_clock::now();
    rpc.close();
    requester.join();
    auto elapsed = std::chrono::steady_clock::now() - start;

    INFO("close() unblocks in-flight request promptly, not after 5s timeout");
    REQUIRE(elapsed < std::chrono::seconds(2));

    process.shutdown();
    rpc.shutdown();
}

TEST_CASE("nvim rpc request from a worker thread completes successfully", "[rpc]")
{
    ScopedEnvVar env("DRAXUL_RPC_FAKE_MODE", "success");
    NvimProcess process;
    INFO("fake RPC server spawns");
    REQUIRE(process.spawn(helper_path()));

    NvimRpc rpc;
    INFO("rpc initializes");
    REQUIRE(rpc.initialize(process));

    RpcResult result;
    std::thread worker([&rpc, &result]() {
        result = rpc.request("fake_method", { MpackValue::make_int(7) });
    });
    worker.join();

    INFO("worker-thread request reports overall success");
    REQUIRE(result.has_value());
    INFO("worker-thread result payload survives");
    REQUIRE(result.value().as_str() == std::string("ok"));

    rpc.shutdown();
    process.shutdown();
}

TEST_CASE("nvim rpc discards responses with out-of-range msgid and still completes the real request", "[rpc]")
{
    ScopedLogCapture capture;

    ScopedEnvVar env("DRAXUL_RPC_FAKE_MODE", "out_of_range_msgid_then_success");
    NvimProcess process;
    INFO("fake RPC server spawns");
    REQUIRE(process.spawn(helper_path()));

    NvimRpc rpc;
    INFO("rpc initializes");
    REQUIRE(rpc.initialize(process));

    auto start = std::chrono::steady_clock::now();
    RpcResult result = rpc.request("fake_method", { MpackValue::make_int(7) });
    auto elapsed = std::chrono::steady_clock::now() - start;

    rpc.shutdown();
    process.shutdown();

    INFO("real response still arrives after the poisoned out-of-range msgid is discarded");
    REQUIRE(result.has_value());
    REQUIRE(result.value().as_str() == std::string("ok"));
    INFO("client did not fall back to the 5-second timeout path");
    REQUIRE(elapsed < std::chrono::seconds(2));
    INFO("out-of-range msgid produced an rpc warning log");
    REQUIRE(has_log_message(capture.records, LogLevel::Warn, LogCategory::Rpc, "out-of-range msgid"));
}

TEST_CASE("nvim rpc logs a warning when the transport aborts before a response arrives", "[rpc]")
{
    ScopedLogCapture capture;

    RpcResult result = run_request_with_mode("abort_after_read");

    INFO("aborted transport reports failure");
    REQUIRE(!result.has_value());
    REQUIRE(result.error().kind == ErrorKind::IoError);
    INFO("aborted request should emit an rpc warning");
    REQUIRE(has_log_message(capture.records, LogLevel::Warn, LogCategory::Rpc, "Request timed out or aborted: fake_method"));
}

TEST_CASE("nvim rpc notify write failure fails the transport with a single wake", "[rpc]")
{
    ScopedEnvVar env("DRAXUL_RPC_FAKE_MODE", "close_stdin_until_release");
    TempDir temp("draxul-rpc-notify-write-failure");
    const std::filesystem::path ready_file = temp.path / "ready.txt";
    const std::filesystem::path release_file = temp.path / "release.txt";
    const std::string ready_file_string = ready_file.string();
    const std::string release_file_string = release_file.string();
    ScopedEnvVar ready_env("DRAXUL_RPC_FAKE_READY_FILE", ready_file_string.c_str());
    ScopedEnvVar release_env("DRAXUL_RPC_FAKE_RELEASE_FILE", release_file_string.c_str());

    NvimProcess process;
    INFO("fake RPC server spawns");
    REQUIRE(process.spawn(helper_path()));

    std::atomic<int> notification_callbacks = 0;
    NvimRpc rpc;
    RpcCallbacks callbacks;
    callbacks.on_notification_available = [&]() {
        ++notification_callbacks;
    };

    INFO("rpc initializes");
    REQUIRE(rpc.initialize(process, std::move(callbacks)));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!std::filesystem::exists(ready_file) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(std::filesystem::exists(ready_file));

    // Output is written by the transport's writer thread, so the failure is
    // observed asynchronously. It must wake the owner once (the caller may be
    // idle in its event wait), but further rejected messages must not each
    // produce a spurious wake.
    rpc.notify("fake_notification", { MpackValue::make_int(7) });
    const auto failure_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while ((!rpc.connection_failed() || notification_callbacks.load() == 0)
        && std::chrono::steady_clock::now() < failure_deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    rpc.notify("fake_notification", { MpackValue::make_int(8) });
    rpc.notify("fake_notification", { MpackValue::make_int(9) });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    INFO("write failure is tracked as a connection failure");
    REQUIRE(rpc.connection_failed());
    INFO("the write failure wakes the owner exactly once");
    REQUIRE(notification_callbacks.load() == 1);
    INFO("draining notifications after write failure is empty");
    REQUIRE(rpc.drain_notifications().empty());

    {
        std::ofstream release(release_file, std::ios::binary);
        release << "release";
    }

    rpc.shutdown();
    process.shutdown();
}

namespace
{

bool wait_for_file(const std::filesystem::path& path, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!std::filesystem::exists(path) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return std::filesystem::exists(path);
}

void touch_file(const std::filesystem::path& path)
{
    std::ofstream out(path, std::ios::binary);
    out << "release";
}

// Several times any platform pipe buffer (macOS 64 KiB, Windows 4 KiB).
constexpr size_t kOversizedPayloadBytes = 2 * 1024 * 1024;

} // namespace

// kanban/done/71 cancellable-nvim-output -bug.md: an editor that stops
// reading must not freeze the interface. Before outbound ownership moved to a
// writer thread, the first notify() below blocked the caller until the child
// read again.
TEST_CASE("nvim rpc output to a non-reading child keeps callers responsive and preserves order", "[rpc]")
{
    TempDir temp("draxul-rpc-stalled-output");
    const auto ready_file = temp.path / "ready.txt";
    const auto release_file = temp.path / "release.txt";
    const std::string ready_string = ready_file.string();
    const std::string release_string = release_file.string();
    ScopedEnvVar env("DRAXUL_RPC_FAKE_MODE", "stall_then_echo");
    ScopedEnvVar ready_env("DRAXUL_RPC_FAKE_READY_FILE", ready_string.c_str());
    ScopedEnvVar release_env("DRAXUL_RPC_FAKE_RELEASE_FILE", release_string.c_str());

    NvimProcess process;
    REQUIRE(process.spawn(helper_path()));
    NvimRpc rpc;
    REQUIRE(rpc.initialize(process));
    REQUIRE(wait_for_file(ready_file, std::chrono::seconds(5)));

    constexpr int kMessages = 100;
    const auto start = std::chrono::steady_clock::now();
    rpc.notify("seq", { MpackValue::make_int(0), MpackValue::make_str(std::string(kOversizedPayloadBytes, 'p')) });
    for (int i = 1; i < kMessages; ++i)
        rpc.notify("seq", { MpackValue::make_int(i) });
    const auto enqueue_elapsed = std::chrono::steady_clock::now() - start;

    INFO("oversized output to a stalled child returns without waiting for the child");
    CHECK(enqueue_elapsed < std::chrono::seconds(1));
    CHECK_FALSE(rpc.connection_failed());

    touch_file(release_file);

    std::vector<RpcNotification> echoes;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (static_cast<int>(echoes.size()) < kMessages && std::chrono::steady_clock::now() < deadline)
    {
        auto batch = rpc.drain_notifications();
        echoes.insert(echoes.end(), std::make_move_iterator(batch.begin()), std::make_move_iterator(batch.end()));
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    rpc.close();
    process.shutdown();
    rpc.shutdown();

    REQUIRE(static_cast<int>(echoes.size()) == kMessages);
    bool ordered = true;
    for (int i = 0; i < kMessages; ++i)
    {
        const auto& echo = echoes[static_cast<size_t>(i)];
        ordered = ordered && echo.method == "echo" && echo.params.size() == 2 && echo.params[0].as_int() == i;
    }
    INFO("every message reaches the child once, in submission order");
    CHECK(ordered);
    INFO("the oversized payload arrives intact");
    CHECK(echoes[0].params[1].as_int() == static_cast<int64_t>(kOversizedPayloadBytes));
}

TEST_CASE("nvim rpc shutdown is bounded while output is blocked on a non-reading child", "[rpc][shutdown]")
{
    TempDir temp("draxul-rpc-stalled-shutdown");
    const auto ready_file = temp.path / "ready.txt";
    const auto release_file = temp.path / "never-released.txt";
    const std::string ready_string = ready_file.string();
    const std::string release_string = release_file.string();
    ScopedEnvVar env("DRAXUL_RPC_FAKE_MODE", "stall_then_echo");
    ScopedEnvVar ready_env("DRAXUL_RPC_FAKE_READY_FILE", ready_string.c_str());
    ScopedEnvVar release_env("DRAXUL_RPC_FAKE_RELEASE_FILE", release_string.c_str());

    NvimProcess process;
    REQUIRE(process.spawn(helper_path()));
    NvimRpc rpc;
    REQUIRE(rpc.initialize(process));
    REQUIRE(wait_for_file(ready_file, std::chrono::seconds(5)));

    rpc.notify("seq", { MpackValue::make_int(0), MpackValue::make_str(std::string(kOversizedPayloadBytes, 'p')) });
    // Mirror NvimHost::shutdown(): a final quit command queued behind the
    // blocked payload.
    rpc.notify("nvim_input", { MpackValue::make_str("<C-\\><C-n>:qa!<CR>") });
    // Let the writer fill the pipe and block on the stalled child.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const auto close_start = std::chrono::steady_clock::now();
    rpc.close();
    const auto close_elapsed = std::chrono::steady_clock::now() - close_start;
    INFO("close() cancels the blocked write instead of waiting for the child");
    CHECK(close_elapsed < std::chrono::milliseconds(750));
    INFO("a requested close is not reported as a connection failure");
    CHECK_FALSE(rpc.connection_failed());

    const auto shutdown_start = std::chrono::steady_clock::now();
    process.shutdown();
    const auto process_shutdown_elapsed = std::chrono::steady_clock::now() - shutdown_start;
    INFO("process shutdown returns without closing a pipe under a blocked reader");
    CHECK(process_shutdown_elapsed < std::chrono::milliseconds(500));
    rpc.shutdown();
    const auto shutdown_elapsed = std::chrono::steady_clock::now() - shutdown_start;
    INFO("process and transport shutdown complete within the bounded reaping window");
    CHECK(shutdown_elapsed < std::chrono::seconds(4));
}

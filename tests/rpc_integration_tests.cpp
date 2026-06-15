#include "support/scoped_env_var.h"
#include "support/temp_dir.h"
#include "support/test_support.h"

#include <catch2/catch_all.hpp>
#include <atomic>
#include <chrono>
#include <draxul/log.h>
#include <draxul/nvim.h>
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

    RpcResult result = rpc.request("fake_method", { NvimRpc::make_int(7) });
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
        rpc.request("fake_method", { NvimRpc::make_int(7) });
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
        result = rpc.request("fake_method", { NvimRpc::make_int(7) });
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
    RpcResult result = rpc.request("fake_method", { NvimRpc::make_int(7) });
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

TEST_CASE("nvim rpc notify write failure does not signal notification availability", "[rpc]")
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

    rpc.notify("fake_notification", { NvimRpc::make_int(7) });

    INFO("write failure is tracked as a connection failure");
    REQUIRE(rpc.connection_failed());
    INFO("no notification was queued, so the notification callback must not fire");
    REQUIRE(notification_callbacks.load() == 0);
    INFO("draining notifications after write failure is empty");
    REQUIRE(rpc.drain_notifications().empty());

    {
        std::ofstream release(release_file, std::ios::binary);
        release << "release";
    }

    rpc.shutdown();
    process.shutdown();
}

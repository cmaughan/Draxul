#include "support/test_support.h"

#include <catch2/catch_all.hpp>
#include <chrono>
#include <cstdlib>
#include <draxul/log.h>
#include <draxul/nvim.h>
#include <filesystem>
#include <string>
#include <thread>

using namespace draxul;
using namespace draxul::tests;

namespace
{

class ScopedEnvVar
{
public:
    ScopedEnvVar(const char* name, const char* value)
        : name_(name)
    {
        const char* existing = std::getenv(name_);
        if (existing)
        {
            had_original_ = true;
            original_ = existing;
        }
        set(value);
    }

    ~ScopedEnvVar()
    {
        if (had_original_)
            set(original_.c_str());
        else
            clear();
    }

private:
    void set(const char* value)
    {
#ifdef _WIN32
        _putenv_s(name_, value);
#else
        setenv(name_, value, 1);
#endif
    }

    void clear()
    {
#ifdef _WIN32
        _putenv_s(name_, "");
#else
        unsetenv(name_);
#endif
    }

    const char* name_;
    bool had_original_ = false;
    std::string original_;
};

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

    INFO("request reports transport success");
    REQUIRE(result.transport_ok);
    INFO("request reports overall success");
    REQUIRE(result.ok());
    INFO("request result payload survives");
    REQUIRE(result.result.as_str() == std::string("ok"));
    INFO("successful response keeps nil error");
    REQUIRE(result.error.is_nil());
}

TEST_CASE("nvim rpc surfaces remote errors without losing transport success", "[rpc]")
{
    RpcResult result = run_request_with_mode("error");

    INFO("remote error still counts as transport success");
    REQUIRE(result.transport_ok);
    INFO("remote error makes the request unsuccessful");
    REQUIRE(!result.ok());
    INFO("remote error is surfaced");
    REQUIRE(result.is_error());
    INFO("remote error payload survives");
    REQUIRE(result.error.as_str() == std::string("boom"));
}

TEST_CASE("nvim rpc aborts cleanly when the child exits without responding", "[rpc]")
{
    auto start = std::chrono::steady_clock::now();
    RpcResult result = run_request_with_mode("abort_after_read");
    auto elapsed = std::chrono::steady_clock::now() - start;

    INFO("aborted transport reports failure");
    REQUIRE(!result.transport_ok);
    INFO("aborted transport is not successful");
    REQUIRE(!result.ok());
    INFO("aborted transport returns promptly without timing out");
    REQUIRE(elapsed < std::chrono::seconds(2));
}

TEST_CASE("nvim rpc queues notifications received before the response", "[rpc]")
{
    std::vector<RpcNotification> notifications;
    RpcResult result = run_request_with_mode("notify_then_success", &notifications);

    INFO("request still succeeds when a notification precedes the response");
    REQUIRE(result.ok());
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
    REQUIRE(!result.transport_ok);
    INFO("malformed response is not successful");
    REQUIRE(!result.ok());
    INFO("malformed response returns promptly without timing out");
    REQUIRE(elapsed < std::chrono::seconds(2));
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

    INFO("worker-thread request reports transport success");
    REQUIRE(result.transport_ok);
    INFO("worker-thread request reports overall success");
    REQUIRE(result.ok());
    INFO("worker-thread result payload survives");
    REQUIRE(result.result.as_str() == std::string("ok"));

    rpc.shutdown();
    process.shutdown();
}

TEST_CASE("nvim rpc logs a warning when the transport aborts before a response arrives", "[rpc]")
{
    ScopedLogCapture capture;

    RpcResult result = run_request_with_mode("abort_after_read");

    INFO("aborted transport reports failure");
    REQUIRE(!result.transport_ok);
    INFO("aborted request should emit an rpc warning");
    REQUIRE(has_log_message(capture.records, LogLevel::Warn, LogCategory::Rpc, "Request timed out or aborted: fake_method"));
}

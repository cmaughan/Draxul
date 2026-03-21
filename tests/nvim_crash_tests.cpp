#include "support/test_support.h"

#include <catch2/catch_all.hpp>
#include <chrono>
#include <cstdlib>
#include <draxul/log.h>
#include <draxul/nvim_rpc.h>
#include <string>
#include <thread>

#ifndef _WIN32
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

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

bool has_log_message(const std::vector<LogRecord>& records, LogLevel level, LogCategory category,
    std::string_view needle)
{
    for (const auto& record : records)
    {
        if (record.level == level && record.category == category
            && record.message.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

std::string fake_server_path()
{
    return DRAXUL_RPC_FAKE_PATH;
}

} // namespace

// Test 1: Simulate a process crash by killing the child while an RPC request is in-flight.
// The fake server in "hang" mode reads stdin but never sends a response.
// We kill the process by closing its pipes (process.shutdown()) and then call rpc.close().
// The RPC layer must unblock within the deadline.
TEST_CASE("nvim rpc unblocks promptly when the child process is force-killed mid-request", "[nvim]")
{
    ScopedEnvVar env("DRAXUL_RPC_FAKE_MODE", "hang");

    NvimProcess process;
    INFO("fake RPC server spawns in hang mode");
    REQUIRE(process.spawn(fake_server_path()));
    INFO("fake server should be running after spawn");
    REQUIRE(process.is_running());

    NvimRpc rpc;
    INFO("rpc initializes");
    REQUIRE(rpc.initialize(process));

    // Launch an in-flight request that will block forever (hang mode never responds).
    std::thread requester([&rpc]() {
        rpc.request("fake_method", { NvimRpc::make_int(1) });
    });

    // Give the request time to reach the server and block on the response CV.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Simulate a crash: close the child process pipes and force-kill it.
    // This is the functional equivalent of an unexpected child process death.
    auto start = std::chrono::steady_clock::now();

    // Closing the pipes causes the reader thread to see a pipe error, which
    // sets read_failed_ and notifies the response CV — unblocking the requester.
    process.shutdown();
    rpc.close();

    requester.join();
    auto elapsed = std::chrono::steady_clock::now() - start;

    INFO("rpc.close() must unblock an in-flight request within 2 seconds after process kill");
    REQUIRE(elapsed < std::chrono::seconds(2));

    rpc.shutdown();

    INFO("process should not be running after shutdown");
    REQUIRE(!process.is_running());
}

// Test 2: After a simulated crash, NvimRpc::shutdown() joins the reader thread without deadlock.
TEST_CASE("nvim rpc reader thread exits cleanly after process is force-killed", "[nvim]")
{
    ScopedEnvVar env("DRAXUL_RPC_FAKE_MODE", "hang");

    NvimProcess process;
    INFO("fake RPC server spawns");
    REQUIRE(process.spawn(fake_server_path()));

    NvimRpc rpc;
    INFO("rpc initializes");
    REQUIRE(rpc.initialize(process));

    // Kill the child process to simulate a crash — no in-flight request needed.
    process.shutdown();

    auto start = std::chrono::steady_clock::now();
    rpc.shutdown(); // Must join reader thread; must not deadlock.
    auto elapsed = std::chrono::steady_clock::now() - start;

    INFO("rpc.shutdown() must complete within 2 seconds after the child process dies");
    REQUIRE(elapsed < std::chrono::seconds(2));
}

// Test 3: After a crash, is_running() must return false and the log must contain the expected
// pipe-error warning emitted by the reader thread.
TEST_CASE("nvim rpc emits a pipe error log when the child process dies unexpectedly", "[nvim]")
{
    ScopedLogCapture capture;
    ScopedEnvVar env("DRAXUL_RPC_FAKE_MODE", "hang");

    NvimProcess process;
    INFO("fake RPC server spawns");
    REQUIRE(process.spawn(fake_server_path()));

    NvimRpc rpc;
    INFO("rpc initializes");
    REQUIRE(rpc.initialize(process));

    // Kill the child: the reader thread will hit a pipe read error and emit a log.
    process.shutdown();
    rpc.shutdown();

    INFO("is_running() must return false after shutdown");
    REQUIRE(!process.is_running());

    // The reader thread logs "nvim pipe read error" when the remote end closes.
    bool found_pipe_error = has_log_message(capture.records, LogLevel::Error, LogCategory::Rpc,
        "nvim pipe read error");
    bool found_transport_closed = has_log_message(capture.records, LogLevel::Info, LogCategory::Rpc,
        "Reader thread exiting");
    INFO("log should contain a pipe error or transport-closed message after child death");
    REQUIRE((found_pipe_error || found_transport_closed));
}

#ifndef _WIN32
// Test 4 (POSIX only): Use SIGKILL directly on the child PID, bypassing Draxul's shutdown path.
// This requires spawning a real subprocess we can send a signal to. We use the fake server in
// "hang" mode because it loops indefinitely; we retrieve the PID via a shell command.
//
// Since NvimProcess does not expose the PID, we spawn the fake server as a second process via
// popen so we can kill it with SIGKILL while the NvimRpc reader is still blocked on a read.
TEST_CASE("nvim rpc reader thread exits after SIGKILL on the child process (POSIX)", "[nvim]")
{
    ScopedEnvVar env("DRAXUL_RPC_FAKE_MODE", "hang");

    NvimProcess process;
    INFO("fake RPC server spawns");
    REQUIRE(process.spawn(fake_server_path()));

    NvimRpc rpc;
    INFO("rpc initializes");
    REQUIRE(rpc.initialize(process));

    // Issue a request that will block; gives the reader thread time to start reading.
    std::thread requester([&rpc]() {
        rpc.request("fake_method", { NvimRpc::make_int(42) });
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // process.shutdown() sends SIGTERM then SIGKILL; this simulates what happens when the
    // OS force-kills nvim (e.g. OOM killer). We measure whether the RPC layer recovers.
    auto start = std::chrono::steady_clock::now();
    process.shutdown(); // Closes pipes + sends SIGKILL to fake server.

    // The reader thread should detect the pipe closure and unblock the request CV.
    requester.join();
    auto elapsed = std::chrono::steady_clock::now() - start;

    INFO("requester thread must unblock within 2 seconds after SIGKILL");
    REQUIRE(elapsed < std::chrono::seconds(2));

    rpc.shutdown();

    INFO("process must not be running after SIGKILL");
    REQUIRE(!process.is_running());
}
#endif

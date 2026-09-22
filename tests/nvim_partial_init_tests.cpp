// WI 04 — NvimHost partial init should not std::terminate.
//
// Before the fix, NvimRpc's reader thread and UiRequestWorker's worker thread
// were joinable std::thread members with default destructors. If NvimHost
// initialization failed partway through (e.g. nvim_ui_attach rejected) the
// host was destroyed without an explicit shutdown() and the joinable threads
// tripped std::terminate().
//
// These tests exercise the safety-net destructors directly, plus the full
// initialize/destroy cycle against the fake RPC server, to lock the fix in.

#include "support/scoped_env_var.h"

#include <catch2/catch_all.hpp>
#include <chrono>
#include <draxul/nvim_transport.h>
#include <draxul/ui_request_worker.h>
#include <thread>

using namespace draxul;
using namespace draxul::tests;

namespace
{
std::string fake_server_path()
{
    return DRAXUL_RPC_FAKE_PATH;
}
} // namespace

TEST_CASE("NvimRpc destructor joins a running reader thread without std::terminate", "[nvim][wi04]")
{
    ScopedEnvVar env("DRAXUL_RPC_FAKE_MODE", "hang");

    NvimProcess process;
    REQUIRE(process.spawn(fake_server_path()));

    // Scope-exit destruction: the reader thread is running, and we never call
    // rpc.shutdown() or rpc.close() explicitly. The destructor must detect the
    // joinable thread and drain it rather than std::terminate().
    {
        NvimRpc rpc;
        REQUIRE(rpc.initialize(process));
        // Let the reader thread actually enter its blocking read.
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        // Killing the child unblocks the reader so the destructor can join
        // quickly; without this the destructor would still be correct but
        // would take longer to unwind.
        process.shutdown();
    } // NvimRpc destructor here — must not terminate.

    SUCCEED("NvimRpc destructor cleanly joined reader thread");
}

TEST_CASE("UiRequestWorker destructor stops a running worker without std::terminate", "[nvim][wi04]")
{
    ScopedEnvVar env("DRAXUL_RPC_FAKE_MODE", "hang");

    NvimProcess process;
    REQUIRE(process.spawn(fake_server_path()));

    NvimRpc rpc;
    REQUIRE(rpc.initialize(process));

    {
        UiRequestWorker worker;
        worker.start(&rpc);
        // Let the worker thread reach its cv.wait.
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    } // Destructor must join, not terminate.

    // Tear down the rest cleanly (the rpc destructor now also has a safety net,
    // but be explicit here so this test isolates the UiRequestWorker behaviour).
    process.shutdown();
    rpc.shutdown();

    SUCCEED("UiRequestWorker destructor cleanly joined worker thread");
}

TEST_CASE("NvimRpc destructor is a no-op when never initialized", "[nvim][wi04]")
{
    // Default-constructed, never initialize()'d — destructor must not block
    // or touch uninitialized state.
    {
        NvimRpc rpc;
    }
    SUCCEED("NvimRpc default destructor completed");
}

TEST_CASE("UiRequestWorker destructor is a no-op when never started", "[nvim][wi04]")
{
    {
        UiRequestWorker worker;
    }
    SUCCEED("UiRequestWorker default destructor completed");
}

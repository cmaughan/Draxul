
#include "support/fake_rpc_channel.h"

#include <draxul/ui_request_worker.h>

#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <string>
#include <vector>

#include <catch2/catch_all.hpp>

using namespace draxul;

namespace
{
using draxul::tests::FakeRpcChannel;

} // namespace

TEST_CASE("ui request worker state coalesces bursts to the latest resize", "[ui]")
{
    UiRequestWorkerState state;
    state.start();

    for (int i = 0; i < 10; ++i)
    {
        INFO("running state should accept resize requests");
        REQUIRE(state.request_resize(80 + i, 24 + i, "burst " + std::to_string(i)));
    }

    INFO("coalesced burst should leave one pending request");
    REQUIRE(state.has_pending_request());
    auto pending = state.take_pending_request();
    INFO("the coalesced request should be retrievable");
    REQUIRE(pending.has_value());
    INFO("coalescing keeps the latest column count");
    REQUIRE(pending->cols == 89);
    INFO("coalescing keeps the latest row count");
    REQUIRE(pending->rows == 33);
    INFO("coalescing keeps the latest reason");
    REQUIRE(pending->reason == std::string("burst 9"));
    INFO("taking the request clears the pending slot");
    REQUIRE(!state.has_pending_request());
}

TEST_CASE("ui request worker stops cleanly after an in-flight resize request", "[ui]")
{
    FakeRpcChannel rpc(true);
    UiRequestWorker worker;
    worker.start(&rpc);
    worker.request_resize(120, 40, "shutdown");

    INFO("worker should begin the in-flight resize request");
    REQUIRE(rpc.wait_for_in_flight(std::chrono::milliseconds(1000)));

    auto stop_future = std::async(std::launch::async, [&]() {
        worker.stop();
        return true;
    });

    rpc.release();
    INFO("stop should join once the in-flight request completes");
    REQUIRE(stop_future.wait_for(std::chrono::milliseconds(1000)) == std::future_status::ready);
    INFO("the worker should issue exactly one RPC request");
    REQUIRE(rpc.request_count() == size_t(1));
    const auto request = rpc.request_at(0);
    INFO("worker uses the resize RPC");
    REQUIRE(request.method == std::string("nvim_ui_try_resize"));
    INFO("worker forwards the requested columns");
    REQUIRE(request.params[0].as_int() == int64_t(120));
    INFO("worker forwards the requested rows");
    REQUIRE(request.params[1].as_int() == int64_t(40));
}

TEST_CASE("ui request worker drops resize requests after stop", "[ui]")
{
    FakeRpcChannel rpc;
    UiRequestWorker worker;
    worker.start(&rpc);
    worker.stop();
    worker.request_resize(90, 30, "after stop");

    INFO("post-stop requests should be ignored");
    REQUIRE(rpc.request_count() == size_t(0));
}


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

class FakeRpcChannel final : public IRpcChannel
{
public:
    struct Call
    {
        std::string method;
        std::vector<MpackValue> params;
    };

    explicit FakeRpcChannel(bool block_requests = false)
        : block_requests_(block_requests)
    {
    }

    RpcResult request(const std::string& method, const std::vector<MpackValue>& params) override
    {
        std::unique_lock<std::mutex> lock(mutex_);
        requests.push_back({ method, params });
        request_in_flight_ = true;
        cv_.notify_all();

        if (block_requests_)
            cv_.wait(lock, [&]() { return released_; });

        request_in_flight_ = false;
        cv_.notify_all();

        RpcResult result;
        result.transport_ok = true;
        result.result = NvimRpc::make_nil();
        return result;
    }

    void notify(const std::string&, const std::vector<MpackValue>&) override {}

    bool wait_for_request_count(size_t count, std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, timeout, [&]() { return requests.size() >= count; });
    }

    bool wait_for_in_flight(std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, timeout, [&]() { return request_in_flight_; });
    }

    void release()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        released_ = true;
        cv_.notify_all();
    }

    size_t request_count() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return requests.size();
    }

    Call request_at(size_t index) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return requests[index];
    }

private:
    const bool block_requests_ = false;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool request_in_flight_ = false;
    bool released_ = false;

public:
    std::vector<Call> requests;
};

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

// Tests for UiRequestWorker overlapping-request, coalescing, cancellation,
// and shutdown semantics. Complements the simpler cases in
// ui_request_worker_tests.cpp and the race tests in shutdown_race_tests.cpp.
//
// Threading model under test:
//   - UiRequestWorker owns a single worker thread.
//   - Pending requests are stored in a single "latest-wins" slot via
//     UiRequestWorkerState (not a FIFO queue).
//   - While a request is in-flight on the worker thread, newer requests
//     update the pending slot; the worker picks up the newest value after
//     the in-flight call returns.
//   - stop() sets running_=false, notifies the cv, and joins. Any pending
//     (not yet picked up) request is dropped. An in-flight request runs to
//     completion before the worker thread exits.

#include <draxul/nvim_protocol.h>
#include <draxul/ui_request_worker.h>

#include <atomic>
#include <catch2/catch_all.hpp>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <string>
#include <vector>

using namespace draxul;

namespace
{

// A gateable fake RPC channel. Each call to request() records its method+
// params, signals waiters, and then blocks on a per-index gate until
// release_one() (or release_all()) is called. This lets the test observe
// "request A is in-flight; now submit B" scenarios deterministically.
class GatedFakeRpc final : public IRpcChannel
{
public:
    struct Call
    {
        std::string method;
        int cols = 0;
        int rows = 0;
        std::string reason_if_known;
    };

    RpcResult request(const std::string& method, const std::vector<MpackValue>& params) override
    {
        int my_index = 0;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            Call call;
            call.method = method;
            if (params.size() >= 2)
            {
                call.cols = static_cast<int>(params[0].as_int());
                call.rows = static_cast<int>(params[1].as_int());
            }
            calls_.push_back(std::move(call));
            my_index = static_cast<int>(calls_.size()) - 1;
            in_flight_.fetch_add(1);
            arrived_cv_.notify_all();

            released_cv_.wait(lock, [this, my_index]() { return released_through_ > my_index || released_all_; });

            in_flight_.fetch_sub(1);
            arrived_cv_.notify_all();
        }

        return RpcResult::ok(MpackValue::make_nil());
    }

    void notify(const std::string&, const std::vector<MpackValue>&) override {}

    // Wait until at least `count` total requests have been observed.
    bool wait_for_request_count(int count, std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return arrived_cv_.wait_for(lock, timeout, [this, count]() { return static_cast<int>(calls_.size()) >= count; });
    }

    // Wait until no request is currently in-flight.
    bool wait_for_idle(std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return arrived_cv_.wait_for(lock, timeout, [this]() { return in_flight_.load() == 0; });
    }

    // Release the i-th request (and all prior).
    void release_through(int index)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (index + 1 > released_through_)
            released_through_ = index + 1;
        released_cv_.notify_all();
    }

    void release_all()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        released_all_ = true;
        released_cv_.notify_all();
    }

    int current_in_flight() const
    {
        return in_flight_.load();
    }

    int call_count() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int>(calls_.size());
    }

    Call call_at(int index) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return calls_.at(static_cast<size_t>(index));
    }

    std::vector<Call> snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return calls_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable arrived_cv_;
    std::condition_variable released_cv_;
    std::vector<Call> calls_;
    std::atomic<int> in_flight_{ 0 };
    int released_through_ = 0; // number of calls released so far
    bool released_all_ = false;
};

} // namespace

// -----------------------------------------------------------------------
// Overlapping: a second request submitted while the first is in-flight
// must not be processed before the first one completes. The worker's
// latest-wins slot must then deliver the newest value after the in-flight
// call returns.
// -----------------------------------------------------------------------
TEST_CASE("UiRequestWorker does not start a second request before the in-flight one completes", "[ui][overlap]")
{
    GatedFakeRpc rpc;
    UiRequestWorker worker;
    worker.start(&rpc);

    // Submit A; wait until it is actually in-flight.
    worker.request_resize(100, 30, "A");
    INFO("first request should reach the RPC within 1s");
    REQUIRE(rpc.wait_for_request_count(1, std::chrono::milliseconds(1000)));
    REQUIRE(rpc.current_in_flight() == 1);

    // While A is blocked, submit B. B must NOT start yet — only one call
    // should ever be in-flight on the single worker thread.
    worker.request_resize(110, 32, "B");

    // Give B a chance to (incorrectly) race ahead.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    INFO("only the first request should be in-flight while A is blocked");
    REQUIRE(rpc.call_count() == 1);
    REQUIRE(rpc.current_in_flight() == 1);

    // Release A; the worker should then pick up B and call RPC again.
    rpc.release_through(0);
    INFO("second request should reach the RPC after the first is released");
    REQUIRE(rpc.wait_for_request_count(2, std::chrono::milliseconds(1000)));

    // Release B so stop() can join.
    rpc.release_all();
    worker.stop();

    INFO("exactly two RPC calls observed");
    REQUIRE(rpc.call_count() == 2);
    INFO("first call uses A's geometry");
    REQUIRE(rpc.call_at(0).cols == 100);
    REQUIRE(rpc.call_at(0).rows == 30);
    INFO("second call uses B's geometry");
    REQUIRE(rpc.call_at(1).cols == 110);
    REQUIRE(rpc.call_at(1).rows == 32);
}

// -----------------------------------------------------------------------
// Coalescing during in-flight: bursts submitted while A is blocked should
// collapse to a single follow-up call using the latest values.
// -----------------------------------------------------------------------
TEST_CASE("UiRequestWorker coalesces a burst submitted during an in-flight request", "[ui][overlap]")
{
    GatedFakeRpc rpc;
    UiRequestWorker worker;
    worker.start(&rpc);

    worker.request_resize(80, 24, "A");
    INFO("first request should reach the RPC within 1s");
    REQUIRE(rpc.wait_for_request_count(1, std::chrono::milliseconds(1000)));

    // Burst of 10 coalesced requests while A is in-flight.
    for (int i = 0; i < 10; ++i)
        worker.request_resize(200 + i, 60 + i, "burst-" + std::to_string(i));

    // Confirm the burst did not cause any extra RPC calls yet.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    REQUIRE(rpc.call_count() == 1);

    rpc.release_all();

    // Exactly one follow-up call should be issued, with the latest values.
    REQUIRE(rpc.wait_for_request_count(2, std::chrono::milliseconds(1000)));
    REQUIRE(rpc.wait_for_idle(std::chrono::milliseconds(1000)));

    // Let the worker thread loop settle — if any extra coalesced request
    // were going to appear it would do so within a brief window.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    worker.stop();

    INFO("burst should collapse to exactly one follow-up call");
    REQUIRE(rpc.call_count() == 2);
    INFO("follow-up call should carry the latest values from the burst");
    REQUIRE(rpc.call_at(1).cols == 209);
    REQUIRE(rpc.call_at(1).rows == 69);
}

// -----------------------------------------------------------------------
// Cancellation before the worker starts the RPC: if stop() is called
// while the in-flight call is still blocked and more requests are pending,
// those pending requests must NOT execute after the in-flight call
// releases.
// -----------------------------------------------------------------------
TEST_CASE("UiRequestWorker drops pending requests when stopped during an in-flight call", "[ui][overlap]")
{
    GatedFakeRpc rpc;
    UiRequestWorker worker;
    worker.start(&rpc);

    // Start A and wait for it to be in-flight.
    worker.request_resize(120, 40, "A");
    REQUIRE(rpc.wait_for_request_count(1, std::chrono::milliseconds(1000)));

    // Queue a follow-up while A is still blocked.
    worker.request_resize(121, 41, "B-should-be-dropped");

    // Call stop() from another thread; it must wait for A to finish
    // but should drop the pending B.
    auto stop_future = std::async(std::launch::async, [&]() {
        worker.stop();
        return true;
    });

    // Give stop() a moment to transition running_ -> false.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    INFO("stop() should still be waiting for the in-flight request");
    REQUIRE(stop_future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready);

    // Release A. stop() should then join and B should never run.
    rpc.release_all();

    INFO("stop() must join within 1s once the in-flight request releases");
    REQUIRE(stop_future.wait_for(std::chrono::milliseconds(1000)) == std::future_status::ready);

    // Small settle window in case a spurious second call were about to be issued.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    INFO("pending request submitted before stop() must be dropped");
    REQUIRE(rpc.call_count() == 1);
    REQUIRE(rpc.call_at(0).cols == 120);
    REQUIRE(rpc.call_at(0).rows == 40);
}

// -----------------------------------------------------------------------
// Shutdown with non-empty queue, before any request has started: stop()
// called immediately after a submission but before the worker thread has
// picked it up must drop the pending request cleanly.
// -----------------------------------------------------------------------
TEST_CASE("UiRequestWorker shutdown drops pending request that never started", "[ui][overlap]")
{
    // We run this multiple times because the outcome depends on whether
    // the worker thread wakes before stop() acquires the mutex. Either
    // outcome is valid; the test verifies no crash and a clean shutdown
    // with at most one RPC call.
    for (int iteration = 0; iteration < 20; ++iteration)
    {
        GatedFakeRpc rpc;
        UiRequestWorker worker;
        worker.start(&rpc);
        worker.request_resize(90, 30, "immediate-stop");
        // Pre-release so that if the worker does pick it up, request()
        // will not block.
        rpc.release_all();
        worker.stop();

        INFO("iteration " << iteration << " must produce at most one RPC call");
        REQUIRE(rpc.call_count() <= 1);
        if (rpc.call_count() == 1)
        {
            REQUIRE(rpc.call_at(0).cols == 90);
            REQUIRE(rpc.call_at(0).rows == 30);
        }
    }
}

// -----------------------------------------------------------------------
// Sanity: request_resize submitted after stop() returns no RPC call and
// does not crash (complements the similar test in ui_request_worker_tests
// but exercised through the gated fake).
// -----------------------------------------------------------------------
TEST_CASE("UiRequestWorker ignores post-stop resize requests", "[ui][overlap]")
{
    GatedFakeRpc rpc;
    UiRequestWorker worker;
    worker.start(&rpc);
    worker.stop();

    worker.request_resize(77, 22, "after-stop");

    // Small window for any spurious RPC.
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    INFO("no RPC calls should occur after stop()");
    REQUIRE(rpc.call_count() == 0);
}

// -----------------------------------------------------------------------
// Restart: stop() + start() must leave the worker in a clean state that
// accepts new requests without replaying any prior pending ones.
// -----------------------------------------------------------------------
TEST_CASE("UiRequestWorker start after stop accepts new requests cleanly", "[ui][overlap]")
{
    GatedFakeRpc rpc;
    UiRequestWorker worker;

    worker.start(&rpc);
    worker.request_resize(80, 24, "first-session");
    REQUIRE(rpc.wait_for_request_count(1, std::chrono::milliseconds(1000)));
    rpc.release_all();
    worker.stop();
    REQUIRE(rpc.call_count() == 1);

    // Restart with a fresh fake channel.
    GatedFakeRpc rpc2;
    worker.start(&rpc2);
    worker.request_resize(100, 32, "second-session");
    REQUIRE(rpc2.wait_for_request_count(1, std::chrono::milliseconds(1000)));
    rpc2.release_all();
    worker.stop();

    INFO("second session should see exactly one new RPC call");
    REQUIRE(rpc2.call_count() == 1);
    REQUIRE(rpc2.call_at(0).cols == 100);
    REQUIRE(rpc2.call_at(0).rows == 32);
}

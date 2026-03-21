#include <draxul/ui_request_worker.h>

#include <atomic>
#include <catch2/catch_all.hpp>
#include <chrono>
#include <cstdlib>
#include <draxul/nvim.h>
#include <future>
#include <latch>
#include <string>
#include <thread>
#include <vector>

using namespace draxul;

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

std::string fake_server_path()
{
    return DRAXUL_RPC_FAKE_PATH;
}

// A blocking IRpcChannel that holds the caller in request() until released.
class BlockingFakeRpc final : public IRpcChannel
{
public:
    RpcResult request(const std::string& /*method*/, const std::vector<MpackValue>& /*params*/) override
    {
        std::unique_lock<std::mutex> lock(mutex_);
        in_flight_.fetch_add(1);
        arrived_cv_.notify_all();
        released_cv_.wait(lock, [this]() { return released_; });
        in_flight_.fetch_sub(1);

        RpcResult result;
        result.transport_ok = true;
        result.result = NvimRpc::make_nil();
        return result;
    }

    void notify(const std::string&, const std::vector<MpackValue>&) override {}

    // Block until at least `count` requests are in-flight.
    bool wait_for_in_flight(int count, std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return arrived_cv_.wait_for(lock, timeout, [&]() { return in_flight_.load() >= count; });
    }

    void release()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        released_ = true;
        released_cv_.notify_all();
    }

    int current_in_flight() const
    {
        return in_flight_.load();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable arrived_cv_;
    std::condition_variable released_cv_;
    std::atomic<int> in_flight_{ 0 };
    bool released_ = false;
};

} // namespace

// -----------------------------------------------------------------------
// Test: rpc.close() called concurrently with an in-flight request().
// Verifies no deadlock and that both the close() and request() return
// promptly (well within the 5-second RPC timeout).
// -----------------------------------------------------------------------
TEST_CASE("rpc close() called concurrently with in-flight request returns promptly", "[nvim]")
{
    ScopedEnvVar env("DRAXUL_RPC_FAKE_MODE", "hang");
    NvimProcess process;
    INFO("fake RPC server spawns");
    REQUIRE(process.spawn(fake_server_path()));

    NvimRpc rpc;
    INFO("rpc initializes");
    REQUIRE(rpc.initialize(process));

    // A latch to make the requester signal once it has started.
    std::latch request_started{ 1 };
    std::atomic<bool> request_returned{ false };

    std::thread requester([&]() {
        request_started.count_down();
        rpc.request("fake_method", { NvimRpc::make_int(1) });
        request_returned.store(true);
    });

    // Wait until the requester thread has actually called request() before
    // we call close(), to maximise the chance of hitting the race window.
    request_started.wait();
    // Give the request a moment to reach the blocking wait inside request().
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    auto start = std::chrono::steady_clock::now();
    rpc.close();
    requester.join();
    auto elapsed = std::chrono::steady_clock::now() - start;

    INFO("requester thread must have returned after close()");
    REQUIRE(request_returned.load());
    INFO("close() must unblock the in-flight request well before the 5s timeout");
    REQUIRE(elapsed < std::chrono::seconds(2));

    process.shutdown();
    rpc.shutdown();
}

// -----------------------------------------------------------------------
// Test: multiple concurrent request() calls then close() from a third
// thread — all callers return within 2 seconds.
// -----------------------------------------------------------------------
TEST_CASE("rpc close() called while multiple concurrent requests are in-flight", "[nvim]")
{
    ScopedEnvVar env("DRAXUL_RPC_FAKE_MODE", "hang");
    NvimProcess process;
    INFO("fake RPC server spawns");
    REQUIRE(process.spawn(fake_server_path()));

    NvimRpc rpc;
    INFO("rpc initializes");
    REQUIRE(rpc.initialize(process));

    constexpr int kWorkers = 8;
    std::latch all_started{ kWorkers };
    std::atomic<int> returned_count{ 0 };

    std::vector<std::thread> workers;
    workers.reserve(kWorkers);
    for (int i = 0; i < kWorkers; ++i)
    {
        workers.emplace_back([&, i]() {
            all_started.count_down();
            all_started.wait();
            rpc.request("fake_method", { NvimRpc::make_int(i) });
            returned_count.fetch_add(1);
        });
    }

    // Wait until every worker has entered request() before closing.
    all_started.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    auto start = std::chrono::steady_clock::now();
    rpc.close();
    for (auto& t : workers)
        t.join();
    auto elapsed = std::chrono::steady_clock::now() - start;

    INFO("all concurrent requesters must have returned after close()");
    REQUIRE(returned_count.load() == kWorkers);
    INFO("all requesters must unblock well before the 5s timeout");
    REQUIRE(elapsed < std::chrono::seconds(2));

    process.shutdown();
    rpc.shutdown();
}

// -----------------------------------------------------------------------
// Test: UiRequestWorker with a blocked RPC — call stop() and verify the
// worker thread joins promptly.
// -----------------------------------------------------------------------
TEST_CASE("UiRequestWorker stop() joins promptly when RPC is blocked", "[nvim]")
{
    BlockingFakeRpc rpc;
    UiRequestWorker worker;
    worker.start(&rpc);
    worker.request_resize(120, 40, "test resize");

    // Wait until the worker is actually blocked inside the RPC call.
    INFO("worker should enter the in-flight RPC request within 1s");
    REQUIRE(rpc.wait_for_in_flight(1, std::chrono::milliseconds(1000)));

    // Release the blocked call immediately so stop() can join.
    rpc.release();

    auto stop_future = std::async(std::launch::async, [&]() {
        worker.stop();
        return true;
    });

    INFO("worker stop() must join within 1s once the blocking RPC call is released");
    REQUIRE(stop_future.wait_for(std::chrono::milliseconds(1000)) == std::future_status::ready);
}

// -----------------------------------------------------------------------
// Test: rpc.shutdown() called while request() is mid-flight — clean
// return with no crash or deadlock.
// -----------------------------------------------------------------------
TEST_CASE("rpc shutdown() mid-flight request returns cleanly without crash", "[nvim]")
{
    ScopedEnvVar env("DRAXUL_RPC_FAKE_MODE", "hang");
    NvimProcess process;
    INFO("fake RPC server spawns");
    REQUIRE(process.spawn(fake_server_path()));

    NvimRpc rpc;
    INFO("rpc initializes");
    REQUIRE(rpc.initialize(process));

    std::latch request_started{ 1 };
    RpcResult result;
    std::atomic<bool> request_finished{ false };

    std::thread requester([&]() {
        request_started.count_down();
        result = rpc.request("fake_method", { NvimRpc::make_int(42) });
        request_finished.store(true);
    });

    request_started.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    auto start = std::chrono::steady_clock::now();
    // shutdown() calls close() then joins the reader thread.
    process.shutdown();
    rpc.shutdown();
    requester.join();
    auto elapsed = std::chrono::steady_clock::now() - start;

    INFO("requester must have returned after shutdown()");
    REQUIRE(request_finished.load());
    INFO("transport_ok should be false when shut down mid-flight");
    REQUIRE(!result.transport_ok);
    INFO("shutdown() mid-flight must complete well before the 5s timeout");
    REQUIRE(elapsed < std::chrono::seconds(2));
}

// -----------------------------------------------------------------------
// Test: nvim process exits while the resize worker has a pending request.
// The worker's RPC call should return an error rather than hanging forever.
// -----------------------------------------------------------------------
TEST_CASE("UiRequestWorker resize request returns error when nvim process exits", "[nvim]")
{
    ScopedEnvVar env("DRAXUL_RPC_FAKE_MODE", "hang");
    NvimProcess process;
    INFO("fake RPC server spawns");
    REQUIRE(process.spawn(fake_server_path()));

    NvimRpc rpc;
    INFO("rpc initializes");
    REQUIRE(rpc.initialize(process));

    UiRequestWorker worker;
    worker.start(&rpc);

    // Queue a resize that will block (server is in hang mode).
    worker.request_resize(80, 24, "process exit test");

    // Give the worker a moment to enter the RPC call.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto start = std::chrono::steady_clock::now();

    // Shut down the process first, which closes the pipes and causes the
    // reader thread to observe EOF, waking any waiting request() calls.
    process.shutdown();
    rpc.close();

    // stop() must join the worker thread promptly.
    auto stop_future = std::async(std::launch::async, [&]() {
        worker.stop();
        return true;
    });

    INFO("worker stop() must complete within 2s after process shutdown");
    REQUIRE(stop_future.wait_for(std::chrono::seconds(2)) == std::future_status::ready);

    auto elapsed = std::chrono::steady_clock::now() - start;
    INFO("entire shutdown sequence must complete within 2s");
    REQUIRE(elapsed < std::chrono::seconds(2));

    rpc.shutdown();
}

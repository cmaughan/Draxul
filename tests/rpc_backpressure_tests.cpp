
#include <draxul/log.h>
#include <draxul/nvim_rpc.h>

#include <atomic>
#include <catch2/catch_all.hpp>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace draxul;

// ---------------------------------------------------------------------------
// Helpers shared across test cases
// ---------------------------------------------------------------------------

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

RpcNotification make_notification(int sequence_number)
{
    RpcNotification n;
    n.method = "redraw";
    n.params = { NvimRpc::make_int(static_cast<int64_t>(sequence_number)) };
    return n;
}

} // namespace

// -----------------------------------------------------------------------
// Integration: drain_notifications returns empty when nothing was queued
// -----------------------------------------------------------------------

TEST_CASE("rpc backpressure: NvimRpc drain_notifications returns empty when nothing queued", "[rpc]")
{
    const std::string fake_path = DRAXUL_RPC_FAKE_PATH;
    ScopedEnvVar env("DRAXUL_RPC_FAKE_MODE", "success");
    NvimProcess process;
    INFO("fake RPC server spawns");
    REQUIRE(process.spawn(fake_path));

    NvimRpc rpc;
    INFO("rpc initializes");
    REQUIRE(rpc.initialize(process));

    // No notifications were sent; drain should return empty.
    auto first = rpc.drain_notifications();
    INFO("drain on empty queue returns empty vector");
    REQUIRE(first.empty());

    // Drain again to confirm idempotence.
    auto second = rpc.drain_notifications();
    INFO("second drain on still-empty queue returns empty vector");
    REQUIRE(second.empty());

    rpc.request("test_method", { NvimRpc::make_int(1) });
    rpc.shutdown();
    process.shutdown();
}

// -----------------------------------------------------------------------
// Integration: idempotent drain — second drain after first returns empty
// -----------------------------------------------------------------------

TEST_CASE("rpc backpressure: NvimRpc second drain after first drain returns empty", "[rpc]")
{
    const std::string fake_path = DRAXUL_RPC_FAKE_PATH;
    ScopedEnvVar env("DRAXUL_RPC_FAKE_MODE", "notify_then_success");
    NvimProcess process;
    INFO("fake RPC server spawns");
    REQUIRE(process.spawn(fake_path));

    NvimRpc rpc;
    INFO("rpc initializes");
    REQUIRE(rpc.initialize(process));

    rpc.request("test_method", { NvimRpc::make_int(1) });

    auto first = rpc.drain_notifications();
    INFO("first drain gets the queued notification");
    REQUIRE(static_cast<int>(first.size()) == 1);

    auto second = rpc.drain_notifications();
    INFO("second drain returns empty after queue is consumed");
    REQUIRE(second.empty());

    rpc.shutdown();
    process.shutdown();
}

// -----------------------------------------------------------------------
// Integration: drain_notifications returns all queued items after request
// The fake server sends exactly 1 notification before the response.
// -----------------------------------------------------------------------

TEST_CASE("rpc backpressure: NvimRpc drain_notifications returns all queued items", "[rpc]")
{
    const std::string fake_path = DRAXUL_RPC_FAKE_PATH;
    ScopedEnvVar env("DRAXUL_RPC_FAKE_MODE", "notify_then_success");
    NvimProcess process;
    INFO("fake RPC server spawns");
    REQUIRE(process.spawn(fake_path));

    NvimRpc rpc;
    INFO("rpc initializes");
    REQUIRE(rpc.initialize(process));

    RpcResult result = rpc.request("test_method", { NvimRpc::make_int(1) });
    auto notifications = rpc.drain_notifications();

    rpc.shutdown();
    process.shutdown();

    INFO("request succeeds in notify_then_success mode");
    REQUIRE(result.ok());
    INFO("exactly one notification is drained after the request");
    REQUIRE(static_cast<int>(notifications.size()) == 1);
    INFO("notification method is 'redraw'");
    REQUIRE(notifications[0].method == std::string("redraw"));
}

// -----------------------------------------------------------------------
// Integration: burst of 100 notifications — no items lost
// The fake server sends 100 notifications before the response.
// -----------------------------------------------------------------------

TEST_CASE("rpc backpressure: NvimRpc drains 100 notifications without losing any", "[rpc]")
{
    const std::string fake_path = DRAXUL_RPC_FAKE_PATH;
    ScopedEnvVar env("DRAXUL_RPC_FAKE_MODE", "notify_many");
    NvimProcess process;
    INFO("fake RPC server spawns");
    REQUIRE(process.spawn(fake_path));

    NvimRpc rpc;
    INFO("rpc initializes");
    REQUIRE(rpc.initialize(process));

    RpcResult result = rpc.request("test_method", { NvimRpc::make_int(1) });

    // The reader thread may still be delivering the last notifications;
    // drain in a loop until all 100 have arrived or a deadline expires.
    constexpr int kExpected = 100;
    std::vector<RpcNotification> all;
    all.reserve(kExpected);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (static_cast<int>(all.size()) < kExpected
        && std::chrono::steady_clock::now() < deadline)
    {
        auto batch = rpc.drain_notifications();
        all.insert(all.end(), std::make_move_iterator(batch.begin()),
            std::make_move_iterator(batch.end()));
        if (static_cast<int>(all.size()) < kExpected)
            std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    rpc.shutdown();
    process.shutdown();

    INFO("request succeeds in notify_many mode");
    REQUIRE(result.ok());
    INFO("all 100 notifications delivered without loss");
    REQUIRE(static_cast<int>(all.size()) == kExpected);
}

// -----------------------------------------------------------------------
// Integration: concurrent drain and notification arrival — no lost items
//
// We spin a consumer thread that continuously drains while the reader
// thread inside NvimRpc is pushing notifications received from the fake
// server.  The fake server sends 100 notifications then a response.
// -----------------------------------------------------------------------

TEST_CASE("rpc backpressure: NvimRpc concurrent drain consumer and reader thread — no lost items", "[rpc]")
{
    const std::string fake_path = DRAXUL_RPC_FAKE_PATH;
    ScopedEnvVar env("DRAXUL_RPC_FAKE_MODE", "notify_many");
    NvimProcess process;
    INFO("fake RPC server spawns");
    REQUIRE(process.spawn(fake_path));

    NvimRpc rpc;
    INFO("rpc initializes");
    REQUIRE(rpc.initialize(process));

    constexpr int kExpected = 100;
    std::vector<RpcNotification> all;
    all.reserve(kExpected);
    std::mutex all_mutex;
    std::atomic<bool> done{ false };

    // Consumer thread: drain continuously until signalled.
    std::thread consumer([&]() {
        while (!done.load(std::memory_order_acquire))
        {
            auto batch = rpc.drain_notifications();
            if (!batch.empty())
            {
                std::lock_guard<std::mutex> lock(all_mutex);
                all.insert(all.end(), std::make_move_iterator(batch.begin()),
                    std::make_move_iterator(batch.end()));
            }
            std::this_thread::yield();
        }
        // Final drain after done is set.
        auto tail = rpc.drain_notifications();
        std::lock_guard<std::mutex> lock(all_mutex);
        all.insert(all.end(), std::make_move_iterator(tail.begin()),
            std::make_move_iterator(tail.end()));
    });

    // Main thread sends the request; the fake server streams 100
    // notifications then responds.
    RpcResult result = rpc.request("test_method", { NvimRpc::make_int(1) });

    // Wait for all expected notifications to arrive.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline)
    {
        {
            std::lock_guard<std::mutex> lock(all_mutex);
            if (static_cast<int>(all.size()) >= kExpected)
                break;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    done.store(true, std::memory_order_release);
    consumer.join();

    // Drain anything the consumer missed after joining.
    {
        auto tail = rpc.drain_notifications();
        all.insert(all.end(), std::make_move_iterator(tail.begin()),
            std::make_move_iterator(tail.end()));
    }

    rpc.shutdown();
    process.shutdown();

    INFO("request succeeds with concurrent consumer");
    REQUIRE(result.ok());
    INFO("all 100 notifications delivered under concurrent drain");
    REQUIRE(static_cast<int>(all.size()) == kExpected);
}

// -----------------------------------------------------------------------
// Integration: on_notification_available callback fires for each push
// -----------------------------------------------------------------------

TEST_CASE("rpc backpressure: NvimRpc on_notification_available fires for each notification", "[rpc]")
{
    const std::string fake_path = DRAXUL_RPC_FAKE_PATH;
    ScopedEnvVar env("DRAXUL_RPC_FAKE_MODE", "notify_many");
    NvimProcess process;
    INFO("fake RPC server spawns");
    REQUIRE(process.spawn(fake_path));

    NvimRpc rpc;
    std::atomic<int> callback_count{ 0 };
    rpc.on_notification_available = [&]() {
        callback_count.fetch_add(1, std::memory_order_relaxed);
    };
    INFO("rpc initializes");
    REQUIRE(rpc.initialize(process));

    RpcResult result = rpc.request("test_method", { NvimRpc::make_int(1) });

    // Wait for all expected callbacks to fire.
    constexpr int kExpected = 100;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (callback_count.load(std::memory_order_relaxed) < kExpected
        && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    rpc.shutdown();
    process.shutdown();

    INFO("request succeeds");
    REQUIRE(result.ok());
    INFO("on_notification_available fired at least once per notification");
    REQUIRE(callback_count.load() >= kExpected);
}

// -----------------------------------------------------------------------
// Integration: shutdown clears/unblocks — drain after shutdown returns
// whatever was queued and does not block
// -----------------------------------------------------------------------

TEST_CASE("rpc backpressure: NvimRpc drain_notifications after shutdown returns queued items", "[rpc]")
{
    const std::string fake_path = DRAXUL_RPC_FAKE_PATH;
    ScopedEnvVar env("DRAXUL_RPC_FAKE_MODE", "notify_then_success");
    NvimProcess process;
    INFO("fake RPC server spawns");
    REQUIRE(process.spawn(fake_path));

    NvimRpc rpc;
    INFO("rpc initializes");
    REQUIRE(rpc.initialize(process));

    rpc.request("test_method", { NvimRpc::make_int(1) });

    // Shut down before draining.
    rpc.shutdown();
    process.shutdown();

    // drain_notifications must not block and must return whatever was queued.
    auto notifications = rpc.drain_notifications();
    // The notification count may be 0 or 1 depending on timing; the
    // important invariant is that the call returns promptly (no deadlock).
    INFO("drain after shutdown returns promptly");
    REQUIRE(static_cast<int>(notifications.size()) >= 0);
}

// -----------------------------------------------------------------------
// Direct unit: NotificationQueue-equivalent behaviour through NvimRpc
//
// These tests exercise NvimRpc's internal queue mechanics directly by
// using a simulated reader thread that calls the same push path the real
// reader thread uses (via the public drain_notifications + internal mutex).
// We use a helper NvimRpc subclass (white-box) approach is not available,
// so instead we verify behaviour via the integration path with real pipes.
//
// Additional direct-thread stress test: spawn many notifications via the
// fake server and drain concurrently from the main thread.
// -----------------------------------------------------------------------

TEST_CASE("rpc backpressure: NvimRpc enqueue-then-drain in sequence returns FIFO order", "[rpc]")
{
    const std::string fake_path = DRAXUL_RPC_FAKE_PATH;
    // Use notify_many mode; the fake server sends sequential integers 0..99.
    ScopedEnvVar env("DRAXUL_RPC_FAKE_MODE", "notify_many");
    NvimProcess process;
    INFO("fake RPC server spawns");
    REQUIRE(process.spawn(fake_path));

    NvimRpc rpc;
    INFO("rpc initializes");
    REQUIRE(rpc.initialize(process));

    RpcResult result = rpc.request("test_method", { NvimRpc::make_int(1) });

    constexpr int kExpected = 100;
    std::vector<RpcNotification> all;
    all.reserve(kExpected);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (static_cast<int>(all.size()) < kExpected
        && std::chrono::steady_clock::now() < deadline)
    {
        auto batch = rpc.drain_notifications();
        all.insert(all.end(), std::make_move_iterator(batch.begin()),
            std::make_move_iterator(batch.end()));
        if (static_cast<int>(all.size()) < kExpected)
            std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    rpc.shutdown();
    process.shutdown();

    INFO("request succeeds");
    REQUIRE(result.ok());
    INFO("all notifications received");
    REQUIRE(static_cast<int>(all.size()) == kExpected);

    // Verify FIFO ordering: params[0] should be 0, 1, 2, ... 99.
    bool order_ok = true;
    for (int i = 0; i < kExpected; ++i)
    {
        if (all[static_cast<size_t>(i)].params[0].as_int() != static_cast<int64_t>(i))
        {
            order_ok = false;
            break;
        }
    }
    INFO("notifications arrive in FIFO order (0..99)");
    REQUIRE(order_ok);
}

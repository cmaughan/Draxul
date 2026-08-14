#include <catch2/catch_all.hpp>

#include <draxul/scoreview/mic_player_input.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace draxul::scoreview;
using namespace std::chrono_literals;

namespace
{

bool wait_until(const std::function<bool()>& predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (predicate())
            return true;
        std::this_thread::sleep_for(1ms);
    }
    return predicate();
}

class FakeMicrophoneOps final : public IMicrophoneOps
{
public:
    struct Counts
    {
        int permission = 0;
        int delay = 0;
        int open = 0;
        int resume = 0;
        int available = 0;
        int read = 0;
        int clear = 0;
        int destroy = 0;
        int double_destroy = 0;
        int calls_after_destroy = 0;
    };

    bool initialize(std::string& error) override
    {
        std::lock_guard lock(mutex_);
        if (initialize_ok_)
            return true;
        error = "fake initialization failed";
        return false;
    }

    Permission query_permission() override
    {
        std::unique_lock lock(mutex_);
        ++counts_.permission;
        permission_entered_ = true;
        changed_.notify_all();
        changed_.wait(lock, [this]() { return !block_permission_ || allow_permission_; });
        return permission_;
    }

    void delay_permission_poll() override
    {
        std::unique_lock lock(mutex_);
        ++counts_.delay;
        delay_entered_ = true;
        changed_.notify_all();
        changed_.wait(lock, [this]() { return !block_delay_ || allow_delay_; });
    }

    Stream open_stream(int, std::string& error) override
    {
        std::unique_lock lock(mutex_);
        ++counts_.open;
        open_entered_ = true;
        changed_.notify_all();
        changed_.wait(lock, [this]() { return !block_before_create_ || allow_create_; });
        if (!open_ok_)
        {
            error = "fake open failed";
            return nullptr;
        }
        alive_ = true;
        stream_created_ = true;
        changed_.notify_all();
        changed_.wait(lock, [this]() { return !block_after_create_ || allow_open_return_; });
        return this;
    }

    bool resume_stream(Stream stream, std::string& error) override
    {
        std::unique_lock lock(mutex_);
        check_live(stream);
        ++counts_.resume;
        resume_entered_ = true;
        changed_.notify_all();
        changed_.wait(lock, [this]() { return !block_before_resume_ || allow_resume_; });
        if (resume_ok_)
        {
            resume_succeeded_ = true;
            changed_.notify_all();
            changed_.wait(lock, [this]() { return !block_after_resume_ || allow_resume_return_; });
            return true;
        }
        error = "fake resume failed";
        return false;
    }

    int available_bytes(Stream stream) override
    {
        std::lock_guard lock(mutex_);
        check_live(stream);
        ++counts_.available;
        if (available_override_ >= 0)
            return available_override_;
        return static_cast<int>(samples_.size() * sizeof(float));
    }

    int read_bytes(Stream stream, void* destination, int byte_count) override
    {
        std::lock_guard lock(mutex_);
        check_live(stream);
        ++counts_.read;
        const int copy_bytes = std::min(
            byte_count, static_cast<int>(samples_.size() * sizeof(float)));
        std::memcpy(destination, samples_.data(), static_cast<size_t>(copy_bytes));
        return copy_bytes;
    }

    void clear(Stream stream) override
    {
        std::lock_guard lock(mutex_);
        check_live(stream);
        ++counts_.clear;
    }

    void destroy(Stream stream) override
    {
        std::lock_guard lock(mutex_);
        if (stream != this || !alive_)
        {
            ++counts_.double_destroy;
            return;
        }
        alive_ = false;
        ++counts_.destroy;
        changed_.notify_all();
    }

    void set_permission(Permission permission)
    {
        std::lock_guard lock(mutex_);
        permission_ = permission;
    }

    void fail_initialize()
    {
        std::lock_guard lock(mutex_);
        initialize_ok_ = false;
    }

    void fail_open()
    {
        std::lock_guard lock(mutex_);
        open_ok_ = false;
    }

    void fail_resume()
    {
        std::lock_guard lock(mutex_);
        resume_ok_ = false;
    }

    void block_before_resume()
    {
        std::lock_guard lock(mutex_);
        block_before_resume_ = true;
    }

    void allow_resume()
    {
        std::lock_guard lock(mutex_);
        allow_resume_ = true;
        changed_.notify_all();
    }

    void block_after_resume()
    {
        std::lock_guard lock(mutex_);
        block_after_resume_ = true;
    }

    void allow_resume_return()
    {
        std::lock_guard lock(mutex_);
        allow_resume_return_ = true;
        changed_.notify_all();
    }

    void block_permission()
    {
        std::lock_guard lock(mutex_);
        block_permission_ = true;
    }

    void release_permission()
    {
        std::lock_guard lock(mutex_);
        allow_permission_ = true;
        changed_.notify_all();
    }

    void block_delay()
    {
        std::lock_guard lock(mutex_);
        block_delay_ = true;
    }

    void release_delay()
    {
        std::lock_guard lock(mutex_);
        allow_delay_ = true;
        changed_.notify_all();
    }

    void block_before_create()
    {
        std::lock_guard lock(mutex_);
        block_before_create_ = true;
    }

    void allow_create()
    {
        std::lock_guard lock(mutex_);
        allow_create_ = true;
        changed_.notify_all();
    }

    void block_after_create()
    {
        std::lock_guard lock(mutex_);
        block_after_create_ = true;
    }

    void allow_open_return()
    {
        std::lock_guard lock(mutex_);
        allow_open_return_ = true;
        changed_.notify_all();
    }

    void set_samples(std::vector<float> samples)
    {
        std::lock_guard lock(mutex_);
        samples_ = std::move(samples);
    }

    void set_available_override(int byte_count)
    {
        std::lock_guard lock(mutex_);
        available_override_ = byte_count;
    }

    bool wait_for_permission()
    {
        return wait_for([this]() { return permission_entered_; });
    }

    bool wait_for_delay()
    {
        return wait_for([this]() { return delay_entered_; });
    }

    bool wait_for_open()
    {
        return wait_for([this]() { return open_entered_; });
    }

    bool wait_for_stream_creation()
    {
        return wait_for([this]() { return stream_created_; });
    }

    bool wait_for_resume()
    {
        return wait_for([this]() { return resume_entered_; });
    }

    bool wait_for_resume_success()
    {
        return wait_for([this]() { return resume_succeeded_; });
    }

    bool wait_for_destroy(int expected = 1)
    {
        return wait_for([this, expected]() { return counts_.destroy >= expected; });
    }

    Counts counts() const
    {
        std::lock_guard lock(mutex_);
        return counts_;
    }

private:
    bool wait_for(const std::function<bool()>& predicate)
    {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, 2s, predicate);
    }

    void check_live(Stream stream)
    {
        if (stream != this || !alive_)
            ++counts_.calls_after_destroy;
    }

    mutable std::mutex mutex_;
    std::condition_variable changed_;
    Permission permission_ = Permission::Granted;
    bool initialize_ok_ = true;
    bool open_ok_ = true;
    bool resume_ok_ = true;
    bool block_permission_ = false;
    bool allow_permission_ = false;
    bool permission_entered_ = false;
    bool block_delay_ = false;
    bool allow_delay_ = false;
    bool delay_entered_ = false;
    bool block_before_create_ = false;
    bool allow_create_ = false;
    bool open_entered_ = false;
    bool block_after_create_ = false;
    bool allow_open_return_ = false;
    bool stream_created_ = false;
    bool block_before_resume_ = false;
    bool allow_resume_ = false;
    bool resume_entered_ = false;
    bool block_after_resume_ = false;
    bool allow_resume_return_ = false;
    bool resume_succeeded_ = false;
    bool alive_ = false;
    int available_override_ = -1;
    std::vector<float> samples_;
    Counts counts_;
};

bool wait_for_state(MicPlayerInput& input, MicPlayerInput::State state)
{
    return wait_until([&input, state]() { return input.state() == state; });
}

void check_clean_destroy(const FakeMicrophoneOps::Counts& counts)
{
    CHECK(counts.double_destroy == 0);
    CHECK(counts.calls_after_destroy == 0);
}

} // namespace

TEST_CASE("microphone destruction does not wait for an undecided permission callback",
    "[scoreview][microphone][lifetime]")
{
    FlowController flow;
    auto ops = std::make_shared<FakeMicrophoneOps>();
    ops->block_permission();
    auto input = std::make_unique<MicPlayerInput>(flow, ListenerTuning{}, ops);
    REQUIRE(ops->wait_for_permission());

    const auto start = std::chrono::steady_clock::now();
    input.reset();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start)
                                .count();
    CHECK(elapsed_ms < 250);

    ops->release_permission();
    REQUIRE(wait_until([&ops]() { return ops.use_count() == 1; }));
    const auto counts = ops->counts();
    CHECK(counts.open == 0);
    CHECK(counts.destroy == 0);
    check_clean_destroy(counts);
}

TEST_CASE("microphone abandonment while permission is pending never opens a stream",
    "[scoreview][microphone][lifetime]")
{
    FlowController flow;
    auto ops = std::make_shared<FakeMicrophoneOps>();
    ops->set_permission(IMicrophoneOps::Permission::Pending);
    ops->block_delay();
    auto input = std::make_unique<MicPlayerInput>(flow, ListenerTuning{}, ops);
    REQUIRE(ops->wait_for_delay());

    input.reset();
    ops->release_delay();
    REQUIRE(wait_until([&ops]() { return ops.use_count() == 1; }));
    const auto counts = ops->counts();
    CHECK(counts.open == 0);
    CHECK(counts.destroy == 0);
    check_clean_destroy(counts);
}

TEST_CASE("microphone abandonment after permission destroys a subsequently opened stream",
    "[scoreview][microphone][lifetime]")
{
    FlowController flow;
    auto ops = std::make_shared<FakeMicrophoneOps>();
    ops->block_before_create();
    auto input = std::make_unique<MicPlayerInput>(flow, ListenerTuning{}, ops);
    REQUIRE(ops->wait_for_open());

    input.reset();
    ops->allow_create();
    REQUIRE(ops->wait_for_destroy());
    REQUIRE(wait_until([&ops]() { return ops.use_count() == 1; }));
    const auto counts = ops->counts();
    CHECK(counts.resume == 0);
    CHECK(counts.destroy == 1);
    check_clean_destroy(counts);
}

TEST_CASE("microphone abandonment between stream creation and publication destroys once",
    "[scoreview][microphone][lifetime]")
{
    FlowController flow;
    auto ops = std::make_shared<FakeMicrophoneOps>();
    ops->block_after_create();
    auto input = std::make_unique<MicPlayerInput>(flow, ListenerTuning{}, ops);
    REQUIRE(ops->wait_for_stream_creation());

    input.reset();
    ops->allow_open_return();
    REQUIRE(ops->wait_for_destroy());
    REQUIRE(wait_until([&ops]() { return ops.use_count() == 1; }));
    const auto counts = ops->counts();
    CHECK(counts.resume == 0);
    CHECK(counts.destroy == 1);
    check_clean_destroy(counts);
}

TEST_CASE("microphone destruction does not wait for a blocked resume",
    "[scoreview][microphone][lifetime]")
{
    FlowController flow;
    auto ops = std::make_shared<FakeMicrophoneOps>();
    ops->block_before_resume();
    auto input = std::make_unique<MicPlayerInput>(flow, ListenerTuning{}, ops);
    REQUIRE(ops->wait_for_resume());
    CHECK(input->state() == MicPlayerInput::State::Opening);

    const auto start = std::chrono::steady_clock::now();
    input.reset();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start)
                                .count();
    CHECK(elapsed_ms < 250);
    CHECK(ops->counts().destroy == 0); // worker still owns the local stream

    ops->allow_resume();
    REQUIRE(ops->wait_for_destroy());
    REQUIRE(wait_until([&ops]() { return ops.use_count() == 1; }));
    const auto counts = ops->counts();
    CHECK(counts.resume == 1);
    CHECK(counts.destroy == 1);
    check_clean_destroy(counts);
}

TEST_CASE("microphone abandonment after device resume but before publication destroys once",
    "[scoreview][microphone][lifetime]")
{
    FlowController flow;
    auto ops = std::make_shared<FakeMicrophoneOps>();
    ops->block_after_resume();
    auto input = std::make_unique<MicPlayerInput>(flow, ListenerTuning{}, ops);
    REQUIRE(ops->wait_for_resume_success());
    CHECK(input->state() == MicPlayerInput::State::Opening);

    const auto start = std::chrono::steady_clock::now();
    input.reset();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start)
                                .count();
    CHECK(elapsed_ms < 250);

    ops->allow_resume_return();
    REQUIRE(ops->wait_for_destroy());
    REQUIRE(wait_until([&ops]() { return ops.use_count() == 1; }));
    const auto counts = ops->counts();
    CHECK(counts.resume == 1);
    CHECK(counts.destroy == 1);
    check_clean_destroy(counts);
}

TEST_CASE("microphone destruction after publication takes ownership and destroys once",
    "[scoreview][microphone][lifetime]")
{
    FlowController flow;
    auto ops = std::make_shared<FakeMicrophoneOps>();
    auto input = std::make_unique<MicPlayerInput>(flow, ListenerTuning{}, ops);
    REQUIRE(wait_for_state(*input, MicPlayerInput::State::Ready));
    CHECK(ops->counts().resume == 1);

    input.reset();
    REQUIRE(ops->wait_for_destroy());
    const auto counts = ops->counts();
    CHECK(counts.destroy == 1);
    check_clean_destroy(counts);
}

TEST_CASE("microphone failures publish consistent state and clean up local streams",
    "[scoreview][microphone][failure]")
{
    FlowController flow;

    SECTION("initialization")
    {
        auto ops = std::make_shared<FakeMicrophoneOps>();
        ops->fail_initialize();
        MicPlayerInput input(flow, ListenerTuning{}, ops);
        CHECK(input.state() == MicPlayerInput::State::Failed);
        CHECK(input.error() == "fake initialization failed");
        CHECK(ops->counts().open == 0);
    }

    SECTION("denied permission")
    {
        auto ops = std::make_shared<FakeMicrophoneOps>();
        ops->set_permission(IMicrophoneOps::Permission::Denied);
        MicPlayerInput input(flow, ListenerTuning{}, ops);
        REQUIRE(wait_for_state(input, MicPlayerInput::State::Failed));
        CHECK(input.error().find("permission denied") != std::string::npos);
        CHECK(ops->counts().open == 0);
    }

    SECTION("open")
    {
        auto ops = std::make_shared<FakeMicrophoneOps>();
        ops->fail_open();
        MicPlayerInput input(flow, ListenerTuning{}, ops);
        REQUIRE(wait_for_state(input, MicPlayerInput::State::Failed));
        CHECK(input.error() == "fake open failed");
        CHECK(ops->counts().destroy == 0);
    }

    SECTION("resume")
    {
        auto ops = std::make_shared<FakeMicrophoneOps>();
        ops->fail_resume();
        MicPlayerInput input(flow, ListenerTuning{}, ops);
        REQUIRE(wait_for_state(input, MicPlayerInput::State::Failed));
        CHECK(input.error() == "fake resume failed");
        REQUIRE(ops->wait_for_destroy());
        const auto counts = ops->counts();
        CHECK(counts.destroy == 1);
        check_clean_destroy(counts);
    }
}

TEST_CASE("microphone polling reads fresh data and clears stale backlogs",
    "[scoreview][microphone][poll]")
{
    FlowController flow;

    SECTION("fresh data")
    {
        auto ops = std::make_shared<FakeMicrophoneOps>();
        ops->set_samples(std::vector<float>(128, 0.05f));
        MicPlayerInput input(flow, ListenerTuning{}, ops);
        REQUIRE(wait_for_state(input, MicPlayerInput::State::Ready));
        std::vector<PlayerNoteEvent> events;
        input.poll(10.0, events);
        const auto counts = ops->counts();
        CHECK(counts.available == 1);
        CHECK(counts.read == 1);
        CHECK(counts.clear == 0);
        CHECK(input.level() == Catch::Approx(0.05f));
        check_clean_destroy(counts);
    }

    SECTION("stale backlog")
    {
        auto ops = std::make_shared<FakeMicrophoneOps>();
        ops->set_available_override((44100 * 3 + 1) * static_cast<int>(sizeof(float)));
        MicPlayerInput input(flow, ListenerTuning{}, ops);
        REQUIRE(wait_for_state(input, MicPlayerInput::State::Ready));
        std::vector<PlayerNoteEvent> events;
        input.poll(10.0, events);
        const auto counts = ops->counts();
        CHECK(counts.available == 1);
        CHECK(counts.read == 0);
        CHECK(counts.clear == 1);
        check_clean_destroy(counts);
    }
}

TEST_CASE("microphone repeated construction and destruction has balanced stream ownership",
    "[scoreview][microphone][lifetime]")
{
    FlowController flow;
    for (int iteration = 0; iteration < 25; ++iteration)
    {
        auto ops = std::make_shared<FakeMicrophoneOps>();
        auto input = std::make_unique<MicPlayerInput>(flow, ListenerTuning{}, ops);
        REQUIRE(wait_for_state(*input, MicPlayerInput::State::Ready));
        input.reset();
        REQUIRE(ops->wait_for_destroy());
        const auto counts = ops->counts();
        CHECK(counts.resume == 1);
        CHECK(counts.destroy == 1);
        check_clean_destroy(counts);
    }
}

#include <catch2/catch_all.hpp>
#include <draxul/file_monitor.h>
#include "temp_dir.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

TEST_CASE("native file monitor observes recursive edits and moves across roots", "[file-monitor]")
{
    draxul::tests::TempDir temp("draxul-file-monitor");
    const auto first = temp.path / "first";
    const auto second = temp.path / "second";
    std::filesystem::create_directories(first / "nested");
    std::filesystem::create_directories(second);
    std::mutex mutex;
    std::condition_variable wake;
    std::string error;
    auto monitor = draxul::FileMonitor::create({ first, second }, [&] {
        std::lock_guard lock(mutex);
        wake.notify_one();
    }, error);
    INFO(error);
    REQUIRE(monitor);
    const auto await_change = [&] {
        std::unique_lock lock(mutex);
        return wake.wait_for(lock, 5s, [&] { return monitor->consume_changes(); });
    };
    const auto card = first / "nested" / "card.md";
    std::ofstream(card) << "first";
    REQUIRE(await_change());
    std::ofstream(card, std::ios::app) << " edited";
    REQUIRE(await_change());
    std::filesystem::rename(card, second / "card.md");
    REQUIRE(await_change());
    std::filesystem::remove(second / "card.md");
    REQUIRE(await_change());
    std::filesystem::create_directories(first / "new-lane" / "nested");
    REQUIRE(await_change());
    std::ofstream(first / "new-lane" / "nested" / "new.md") << "new";
    REQUIRE(await_change());
    CHECK(monitor->error().empty());
}

TEST_CASE("file monitor rejects invalid roots without polling", "[file-monitor]")
{
    draxul::tests::TempDir temp("draxul-file-monitor-invalid");
    std::string error;
    CHECK_FALSE(draxul::FileMonitor::create({}, {}, error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(draxul::FileMonitor::create({ temp.path / "missing" }, {}, error));
    CHECK_FALSE(error.empty());
    std::ofstream(temp.path / "file") << "not a directory";
    CHECK_FALSE(draxul::FileMonitor::create({ temp.path / "file" }, {}, error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("file monitor shutdown drains callbacks and interrupts an idle wait", "[file-monitor]")
{
    draxul::tests::TempDir temp("draxul-file-monitor-stop");
    std::atomic<int> callbacks{ 0 };
    std::string error;
    for (int i = 0; i < 10; ++i)
    {
        auto monitor = draxul::FileMonitor::create({ temp.path }, [&] { ++callbacks; }, error);
        INFO(error);
        REQUIRE(monitor);
        std::ofstream(temp.path / "card.md") << i;
        const auto started = std::chrono::steady_clock::now();
        monitor.reset();
        CHECK(std::chrono::steady_clock::now() - started < 2s);
    }
    const int stopped_count = callbacks.load();
    std::ofstream(temp.path / "after-stop.md") << "stopped";
    std::this_thread::sleep_for(100ms);
    CHECK(callbacks.load() == stopped_count);
}

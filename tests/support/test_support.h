#pragma once

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <draxul/log.h>

#if __has_include(<draxul/text_service.h>)
#include <draxul/text_service.h>
#define DRAXUL_TEST_SUPPORT_HAS_TEXT_SERVICE 1
#endif

namespace draxul::tests
{

// ---------------------------------------------------------------------------
// Repository paths — every test target defines DRAXUL_PROJECT_ROOT, which is
// the single supported way to reach checked-in assets. (The historical
// __FILE__-based repo_root() helpers are retired.)
// ---------------------------------------------------------------------------

inline std::filesystem::path project_root()
{
    return std::filesystem::path(DRAXUL_PROJECT_ROOT);
}

inline std::filesystem::path bundled_font_path(
    std::string_view file_name = "JetBrainsMonoNerdFont-Regular.ttf")
{
    return project_root() / "fonts" / file_name;
}

#ifdef DRAXUL_TEST_SUPPORT_HAS_TEXT_SERVICE
// One-line TextService bootstrap with the bundled mono font at 96 DPI.
// Fails the enclosing test if the service cannot initialize.
inline void init_text_service(TextService& service,
    float point_size = TextService::DEFAULT_POINT_SIZE,
    float display_ppi = 96.0f)
{
    TextServiceConfig config;
    config.font_path = bundled_font_path().string();
    INFO("TextService font: " << config.font_path);
    REQUIRE(service.initialize(config, point_size, display_ppi));
}
#endif

// ---------------------------------------------------------------------------
// Polling — the shared replacement for hand-rolled sleep loops. Timeout
// budgets stay at each call site; the poll cadence defaults to 10 ms.
// ---------------------------------------------------------------------------

template <typename Predicate>
bool wait_until(Predicate&& predicate,
    std::chrono::milliseconds timeout = std::chrono::seconds(5),
    std::chrono::milliseconds poll = std::chrono::milliseconds(10))
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true)
    {
        if (predicate())
            return true;
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for(poll);
    }
}

// wait_until for subjects that need explicit pumping between polls (hosts,
// clients, event loops): calls `pump` before each predicate check.
template <typename Pump, typename Predicate>
bool pump_until(Pump&& pump, Predicate&& predicate,
    std::chrono::milliseconds timeout = std::chrono::seconds(5),
    std::chrono::milliseconds poll = std::chrono::milliseconds(10))
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true)
    {
        pump();
        if (predicate())
            return true;
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for(poll);
    }
}

// ---------------------------------------------------------------------------
// File reading — one copy with an explicit contract: an unreadable file fails
// the enclosing test instead of silently returning an empty string.
// ---------------------------------------------------------------------------

inline std::string read_file(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    INFO("read_file: " << path.string());
    REQUIRE(in.good());
    std::string contents((std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());
    REQUIRE_FALSE(in.bad());
    return contents;
}

} // namespace draxul::tests

// ---------------------------------------------------------------------------
// Slow-test gate — the single opt-in switch for expensive fuzz/stress cases.
// ---------------------------------------------------------------------------

#define DRAXUL_SKIP_UNLESS_SLOW()                                               \
    do                                                                          \
    {                                                                           \
        if (std::getenv("DRAXUL_RUN_SLOW_TESTS") == nullptr)                    \
        {                                                                       \
            SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)"); \
        }                                                                       \
    } while (false)

namespace draxul::tests
{

struct ScopedLogCapture
{
    std::vector<LogRecord> records;

    explicit ScopedLogCapture(LogLevel min_level = LogLevel::Info)
    {
        LogOptions options;
        options.min_level = min_level;
        options.enable_stderr = false;
        options.enable_file = false;
        configure_logging(options);
        set_log_sink([this](const LogRecord& record) { records.push_back(record); });
    }

    ~ScopedLogCapture()
    {
        clear_log_sink();
        configure_logging();
    }
};

class TestSkipped : public std::runtime_error
{
public:
    explicit TestSkipped(std::string message)
        : std::runtime_error(std::move(message))
    {
    }
};

inline void expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        throw std::runtime_error(std::string(message));
    }
}

[[noreturn]] inline void skip(std::string_view message)
{
    throw TestSkipped(std::string(message));
}

template <typename T, typename U>
inline void expect_eq(const T& actual, const U& expected, std::string_view message)
{
    if (!(actual == expected))
    {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Fn>
inline void run_test(std::string_view name, Fn&& fn)
{
    try
    {
        fn();
        DRAXUL_LOG_INFO(LogCategory::Test, "[ok] %.*s", static_cast<int>(name.size()), name.data());
    }
    catch (const TestSkipped& ex)
    {
        DRAXUL_LOG_INFO(LogCategory::Test, "[skip] %.*s: %s",
            static_cast<int>(name.size()), name.data(), ex.what());
    }
    catch (const std::exception& ex)
    {
        throw std::runtime_error(std::string(name) + ": " + ex.what());
    }
}

} // namespace draxul::tests

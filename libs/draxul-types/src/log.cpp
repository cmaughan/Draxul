#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <draxul/string_util.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace draxul
{

namespace
{

// Bitmask helper: convert a LogCategory to a single bit position.
inline uint32_t category_bit(LogCategory cat)
{
    return uint32_t{ 1 } << static_cast<uint32_t>(cat);
}

struct LoggerState
{
    std::mutex mutex;
    LogLevel min_level = LogLevel::Info;
    bool enable_stderr = true;
    std::unordered_set<LogCategory> enabled_categories;
    FILE* file = nullptr;
    LogSink sink;

    // Lock-free mirrors for the fast-path check in log_would_emit().
    // 0 means "all categories enabled" (matches empty enabled_categories).
    std::atomic<int> atomic_min_level{ static_cast<int>(LogLevel::Info) };
    std::atomic<uint32_t> atomic_category_mask{ 0 };
};

LoggerState& state()
{
    static LoggerState logger_state;
    return logger_state;
}

bool stderr_is_interactive()
{
    PERF_MEASURE();
#ifdef _WIN32
    int fd = _fileno(stderr);
    return GetConsoleWindow() != nullptr || (fd >= 0 && _isatty(fd));
#else
    return isatty(fileno(stderr)) != 0;
#endif
}

std::optional<LogLevel> parse_log_level(std::string_view value)
{
    PERF_MEASURE();
    std::string lowered = draxul::ascii_lower(value);
    if (lowered == "error")
        return LogLevel::Error;
    if (lowered == "warn" || lowered == "warning")
        return LogLevel::Warn;
    if (lowered == "info")
        return LogLevel::Info;
    if (lowered == "debug")
        return LogLevel::Debug;
    if (lowered == "trace")
        return LogLevel::Trace;
    return std::nullopt;
}

std::optional<LogCategory> parse_log_category(std::string_view value)
{
    PERF_MEASURE();
    std::string lowered = draxul::ascii_lower(value);
    if (lowered == "app")
        return LogCategory::App;
    if (lowered == "rpc")
        return LogCategory::Rpc;
    if (lowered == "nvim")
        return LogCategory::Nvim;
    if (lowered == "window")
        return LogCategory::Window;
    if (lowered == "font")
        return LogCategory::Font;
    if (lowered == "renderer")
        return LogCategory::Renderer;
    if (lowered == "input")
        return LogCategory::Input;
    if (lowered == "test")
        return LogCategory::Test;
    return std::nullopt;
}

std::vector<LogCategory> parse_category_list(std::string_view value)
{
    PERF_MEASURE();
    std::vector<LogCategory> categories;
    size_t start = 0;
    while (start < value.size())
    {
        size_t end = value.find(',', start);
        if (end == std::string_view::npos)
            end = value.size();

        if (std::string token = draxul::trim(std::string(value.substr(start, end - start)));
            !token.empty())
        {
            auto parsed = parse_log_category(token);
            if (parsed)
                categories.push_back(*parsed);
        }

        start = end + 1;
    }
    return categories;
}

bool category_enabled(const LoggerState& logger_state, LogCategory category)
{
    return logger_state.enabled_categories.empty()
        || logger_state.enabled_categories.contains(category);
}

void write_line(FILE* stream, LogLevel level, LogCategory category, const std::string& message)
{
    PERF_MEASURE();
    if (!stream)
        return;

    std::fprintf(stream, "[%s][%s] %s\n", to_string(level), to_string(category), message.c_str());
    std::fflush(stream);
}

} // namespace

const char* to_string(LogLevel level)
{
    using enum LogLevel;
    switch (level)
    {
    case Error:
        return "error";
    case Warn:
        return "warn";
    case Info:
        return "info";
    case Debug:
        return "debug";
    case Trace:
        return "trace";
    }
    return "info";
}

const char* to_string(LogCategory category)
{
    using enum LogCategory;
    switch (category)
    {
    case App:
        return "app";
    case Rpc:
        return "rpc";
    case Nvim:
        return "nvim";
    case Window:
        return "window";
    case Font:
        return "font";
    case Renderer:
        return "renderer";
    case Input:
        return "input";
    case Test:
        return "test";
    }
    return "app";
}

LogLevel parse_log_level_or(std::string_view value, LogLevel fallback)
{
    return parse_log_level(value).value_or(fallback);
}

void configure_logging(const LogOptions& options)
{
    PERF_MEASURE();
    auto& logger_state = state();
    std::scoped_lock lock(logger_state.mutex);

    if (logger_state.file)
    {
        std::fclose(logger_state.file);
        logger_state.file = nullptr;
    }

    logger_state.min_level = options.min_level;
    logger_state.enable_stderr = options.enable_stderr;
    logger_state.enabled_categories.clear();
    uint32_t mask = 0;
    for (LogCategory category : options.enabled_categories)
    {
        logger_state.enabled_categories.insert(category);
        mask |= category_bit(category);
    }

    // Update atomic mirrors so log_would_emit() can read lock-free.
    logger_state.atomic_min_level.store(static_cast<int>(options.min_level));
    logger_state.atomic_category_mask.store(mask);

    if (!options.enable_file || options.file_path.empty())
        return;

    logger_state.file = std::fopen(options.file_path.c_str(), "w");
    if (!logger_state.file)
    {
        std::fprintf(stderr, "[error][app] failed to open log file: %s\n", options.file_path.c_str());
    }
}

void configure_default_logging(const char* default_file_name, bool prefer_file_when_no_console)
{
    PERF_MEASURE();
    LogOptions options;

    // Hoist all getenv calls before using their results — avoids an observed
    // issue on macOS where interleaving getenv with option writes could leave
    // DRAXUL_LOG_FILE unread.
    const char* env_level = std::getenv("DRAXUL_LOG");
    const char* env_categories = std::getenv("DRAXUL_LOG_CATEGORIES");
    const char* env_file = std::getenv("DRAXUL_LOG_FILE");

    if (env_level)
    {
        if (const auto level = parse_log_level(env_level))
            options.min_level = *level;
    }

    if (env_categories)
    {
        options.enabled_categories = parse_category_list(env_categories);
    }

    if (env_file && *env_file != '\0')
    {
        options.enable_file = true;
        options.file_path = env_file;
    }
    else if (!env_file && prefer_file_when_no_console && !stderr_is_interactive())
    {
        options.enable_file = true;
        options.file_path = default_file_name ? default_file_name : "draxul.log";
    }

    configure_logging(options);
}

void shutdown_logging()
{
    PERF_MEASURE();
    auto& logger_state = state();
    std::scoped_lock lock(logger_state.mutex);
    if (logger_state.file)
    {
        std::fclose(logger_state.file);
        logger_state.file = nullptr;
    }
    logger_state.sink = {};
}

bool log_would_emit(LogLevel level, LogCategory category)
{
    PERF_MEASURE();
    auto& logger_state = state();
    // Lock-free fast path: read atomic mirrors with relaxed ordering.
    // Exact ordering is not critical — log level changes are infrequent and
    // a stale read only means one extra or one missed log line transiently.
    if (const int min_level = logger_state.atomic_min_level.load();
        static_cast<int>(level) > min_level)
        return false;
    uint32_t mask = logger_state.atomic_category_mask.load();
    // mask == 0 means "all categories enabled" (mirrors empty enabled_categories).
    return mask == 0 || (mask & category_bit(category)) != 0;
}

void set_log_sink(LogSink sink)
{
    PERF_MEASURE();
    auto& logger_state = state();
    std::scoped_lock lock(logger_state.mutex);
    logger_state.sink = std::move(sink);
}

void clear_log_sink()
{
    set_log_sink({});
}

void log_message(LogLevel level, LogCategory category, std::string_view message)
{
    PERF_MEASURE();
    auto& logger_state = state();
    std::scoped_lock lock(logger_state.mutex);
    if ((int)level > (int)logger_state.min_level || !category_enabled(logger_state, category))
    {
        return;
    }

    LogRecord record = { level, category, std::string(message) };
    if (logger_state.enable_stderr)
    {
        write_line(stderr, level, category, record.message);
    }
    if (logger_state.file)
    {
        write_line(logger_state.file, level, category, record.message);
    }
    if (logger_state.sink)
    {
        logger_state.sink(record);
    }
}

void log_printf(LogLevel level, LogCategory category, const char* fmt, ...)
{
    PERF_MEASURE();
    va_list args;
    va_start(args, fmt);

    va_list copy;
    va_copy(copy, args);
    int size = std::vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);

    if (size < 0)
    {
        va_end(args);
        return;
    }

    std::string message((size_t)size, '\0');
    std::vsnprintf(message.data(), message.size() + 1, fmt, args);
    va_end(args);

    log_message(level, category, message);
}

} // namespace draxul

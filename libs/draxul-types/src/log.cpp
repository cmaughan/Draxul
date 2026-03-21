#include <draxul/log.h>
#include <draxul/string_util.h>

#include <algorithm>
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

struct LoggerState
{
    std::mutex mutex;
    LogLevel min_level = LogLevel::Info;
    bool enable_stderr = true;
    std::unordered_set<LogCategory> enabled_categories;
    FILE* file = nullptr;
    LogSink sink;
};

LoggerState& state()
{
    static LoggerState logger_state;
    return logger_state;
}

bool stderr_is_interactive()
{
#ifdef _WIN32
    int fd = _fileno(stderr);
    return GetConsoleWindow() != nullptr || (fd >= 0 && _isatty(fd));
#else
    return isatty(fileno(stderr)) != 0;
#endif
}

std::optional<LogLevel> parse_log_level(std::string_view value)
{
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
    std::vector<LogCategory> categories;
    size_t start = 0;
    while (start < value.size())
    {
        size_t end = value.find(',', start);
        if (end == std::string_view::npos)
            end = value.size();

        std::string token = draxul::trim(std::string(value.substr(start, end - start)));
        if (!token.empty())
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
        || logger_state.enabled_categories.count(category) > 0;
}

void write_line(FILE* stream, LogLevel level, LogCategory category, const std::string& message)
{
    if (!stream)
        return;

    std::fprintf(stream, "[%s][%s] %s\n", to_string(level), to_string(category), message.c_str());
    std::fflush(stream);
}

} // namespace

const char* to_string(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Error:
        return "error";
    case LogLevel::Warn:
        return "warn";
    case LogLevel::Info:
        return "info";
    case LogLevel::Debug:
        return "debug";
    case LogLevel::Trace:
        return "trace";
    }
    return "info";
}

const char* to_string(LogCategory category)
{
    switch (category)
    {
    case LogCategory::App:
        return "app";
    case LogCategory::Rpc:
        return "rpc";
    case LogCategory::Nvim:
        return "nvim";
    case LogCategory::Window:
        return "window";
    case LogCategory::Font:
        return "font";
    case LogCategory::Renderer:
        return "renderer";
    case LogCategory::Input:
        return "input";
    case LogCategory::Test:
        return "test";
    }
    return "app";
}

void configure_logging(const LogOptions& options)
{
    auto& logger_state = state();
    std::lock_guard<std::mutex> lock(logger_state.mutex);

    if (logger_state.file)
    {
        std::fclose(logger_state.file);
        logger_state.file = nullptr;
    }

    logger_state.min_level = options.min_level;
    logger_state.enable_stderr = options.enable_stderr;
    logger_state.enabled_categories.clear();
    for (LogCategory category : options.enabled_categories)
    {
        logger_state.enabled_categories.insert(category);
    }

    if (options.enable_file && !options.file_path.empty())
    {
        logger_state.file = std::fopen(options.file_path.c_str(), "a");
        if (!logger_state.file)
        {
            std::fprintf(stderr, "[error][app] failed to open log file: %s\n", options.file_path.c_str());
        }
    }
}

void configure_default_logging(const char* default_file_name, bool prefer_file_when_no_console)
{
    LogOptions options;

    if (const char* env_level = std::getenv("DRAXUL_LOG"))
    {
        if (auto level = parse_log_level(env_level))
            options.min_level = *level;
    }

    if (const char* env_categories = std::getenv("DRAXUL_LOG_CATEGORIES"))
    {
        options.enabled_categories = parse_category_list(env_categories);
    }

    if (const char* env_file = std::getenv("DRAXUL_LOG_FILE"))
    {
        if (*env_file != '\0')
        {
            options.enable_file = true;
            options.file_path = env_file;
        }
    }
    else if (prefer_file_when_no_console && !stderr_is_interactive())
    {
        options.enable_file = true;
        options.file_path = default_file_name ? default_file_name : "draxul.log";
    }

    configure_logging(std::move(options));
}

void shutdown_logging()
{
    auto& logger_state = state();
    std::lock_guard<std::mutex> lock(logger_state.mutex);
    if (logger_state.file)
    {
        std::fclose(logger_state.file);
        logger_state.file = nullptr;
    }
    logger_state.sink = {};
}

bool log_would_emit(LogLevel level, LogCategory category)
{
    auto& logger_state = state();
    std::lock_guard<std::mutex> lock(logger_state.mutex);
    return (int)level <= (int)logger_state.min_level && category_enabled(logger_state, category);
}

void set_log_sink(LogSink sink)
{
    auto& logger_state = state();
    std::lock_guard<std::mutex> lock(logger_state.mutex);
    logger_state.sink = std::move(sink);
}

void clear_log_sink()
{
    set_log_sink({});
}

void log_message(LogLevel level, LogCategory category, std::string_view message)
{
    auto& logger_state = state();
    std::lock_guard<std::mutex> lock(logger_state.mutex);
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

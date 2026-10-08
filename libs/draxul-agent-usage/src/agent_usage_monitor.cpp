#include <draxul/agent_usage.h>

#include "usage_time.h"

#include <draxul/file_monitor.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <unordered_set>

namespace draxul
{

// ---------------------------------------------------------------------------
// Time helpers
// ---------------------------------------------------------------------------

std::optional<UsageTimestamp> parse_usage_timestamp(std::string_view text)
{
    const auto digits = [&](size_t offset, size_t count, int& value) {
        if (offset + count > text.size())
            return false;
        value = 0;
        for (size_t index = offset; index < offset + count; ++index)
        {
            const char ch = text[index];
            if (ch < '0' || ch > '9')
                return false;
            value = value * 10 + (ch - '0');
        }
        return true;
    };
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (text.size() < 19 || text[4] != '-' || text[7] != '-' || (text[10] != 'T' && text[10] != ' ')
        || text[13] != ':' || text[16] != ':' || !digits(0, 4, year) || !digits(5, 2, month)
        || !digits(8, 2, day) || !digits(11, 2, hour) || !digits(14, 2, minute)
        || !digits(17, 2, second))
        return std::nullopt;
    const std::chrono::year_month_day date{ std::chrono::year{ year },
        std::chrono::month{ static_cast<unsigned>(month) },
        std::chrono::day{ static_cast<unsigned>(day) } };
    if (!date.ok() || hour > 23 || minute > 59 || second > 60)
        return std::nullopt;

    size_t offset = 19;
    int milliseconds = 0;
    if (offset < text.size() && text[offset] == '.')
    {
        ++offset;
        int scale = 100;
        while (offset < text.size() && text[offset] >= '0' && text[offset] <= '9')
        {
            milliseconds += (text[offset] - '0') * scale;
            scale /= 10;
            ++offset;
        }
    }
    auto result = std::chrono::sys_days{ date } + std::chrono::hours{ hour }
        + std::chrono::minutes{ minute } + std::chrono::seconds{ second }
        + std::chrono::milliseconds{ milliseconds };
    if (offset < text.size() && (text[offset] == '+' || text[offset] == '-'))
    {
        int zone_hours = 0;
        int zone_minutes = 0;
        if (digits(offset + 1, 2, zone_hours))
        {
            const size_t minutes_at = offset + 3 < text.size() && text[offset + 3] == ':' ? offset + 4 : offset + 3;
            digits(minutes_at, 2, zone_minutes);
            const auto zone = std::chrono::hours{ zone_hours } + std::chrono::minutes{ zone_minutes };
            result = text[offset] == '+' ? result - zone : result + zone;
        }
    }
    return std::chrono::time_point_cast<std::chrono::milliseconds>(result);
}

UsageTimestamp file_time_to_usage(std::filesystem::file_time_type time)
{
    const auto system_now = std::chrono::system_clock::now();
    const auto file_now = std::filesystem::file_time_type::clock::now();
    return std::chrono::time_point_cast<std::chrono::milliseconds>(
        system_now + std::chrono::duration_cast<std::chrono::system_clock::duration>(time - file_now));
}

std::string claude_project_directory_name(std::string_view working_directory)
{
    std::string result(working_directory);
    for (char& ch : result)
    {
        const auto byte = static_cast<unsigned char>(ch);
        if (!std::isalnum(byte) || byte >= 0x80)
            ch = '-';
    }
    return result;
}

namespace
{

constexpr auto kRateHistory = std::chrono::minutes{ 2 };
constexpr auto kFutureTolerance = std::chrono::minutes{ 1 };
// A session written shortly before the agent was first observed still belongs
// to it (detection lags launch by up to a refresh or two).
constexpr auto kStartSlack = std::chrono::seconds{ 10 };
constexpr auto kMinimumResolveInterval = std::chrono::seconds{ 5 };
constexpr size_t kCodexMetaLineLimit = 1024 * 1024;
constexpr int kCodexDayLookback = 7;

std::filesystem::path environment_path(const char* name)
{
#if defined(_WIN32)
    const std::wstring wide_name(name, name + std::char_traits<char>::length(name));
    wchar_t* value = nullptr;
    size_t length = 0;
    if (_wdupenv_s(&value, &length, wide_name.c_str()) != 0 || !value)
        return {};
    std::filesystem::path result = *value ? std::filesystem::path(value) : std::filesystem::path{};
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value && *value ? std::filesystem::path(value) : std::filesystem::path{};
#endif
}

std::filesystem::path home_directory()
{
#if defined(_WIN32)
    if (auto profile = environment_path("USERPROFILE"); !profile.empty())
        return profile;
#endif
    return environment_path("HOME");
}

// Native form without trailing separators: Windows reports "C:\dir\".
std::string normalized_directory(std::string_view directory)
{
    if (directory.empty())
        return {};
    std::string text = std::filesystem::path(std::string(directory)).lexically_normal().string();
    while (text.size() > 1 && (text.back() == '/' || text.back() == '\\')
        && !(text.size() == 3 && text[1] == ':'))
        text.pop_back();
    return text;
}

// Comparison key for working directories across tools.
std::string directory_key(std::string_view directory)
{
    std::string text = std::filesystem::path(normalized_directory(directory)).generic_string();
#if defined(_WIN32)
    std::transform(text.begin(), text.end(), text.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
#endif
    return text;
}

bool is_jsonl(const std::filesystem::path& path)
{
    return path.extension() == ".jsonl";
}

std::string two_digits(unsigned value)
{
    return value < 10 ? "0" + std::to_string(value) : std::to_string(value);
}

// Codex groups rollouts in local-date directories: sessions/YYYY/MM/DD.
std::filesystem::path codex_day_directory(
    const std::filesystem::path& root, std::chrono::system_clock::time_point when)
{
    const std::time_t time = std::chrono::system_clock::to_time_t(when);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    return root / std::to_string(local.tm_year + 1900)
        / two_digits(static_cast<unsigned>(local.tm_mon + 1))
        / two_digits(static_cast<unsigned>(local.tm_mday));
}

struct Candidate
{
    std::filesystem::path path;
    UsageTimestamp modified{};
};

std::vector<Candidate> jsonl_files(const std::filesystem::path& directory)
{
    std::vector<Candidate> result;
    std::error_code error;
    for (std::filesystem::directory_iterator iterator(directory, error), end;
         !error && iterator != end; iterator.increment(error))
    {
        const auto& entry = *iterator;
        std::error_code entry_error;
        if (!entry.is_regular_file(entry_error) || !is_jsonl(entry.path()))
            continue;
        const auto modified = entry.last_write_time(entry_error);
        if (!entry_error)
            result.push_back({ entry.path(), file_time_to_usage(modified) });
    }
    return result;
}

std::string read_first_line(const std::filesystem::path& path, size_t limit)
{
    std::ifstream file(path, std::ios::binary);
    std::string line;
    if (!file)
        return line;
    char buffer[16 * 1024];
    while (line.size() < limit)
    {
        file.read(buffer, sizeof(buffer));
        if (file.gcount() <= 0)
            break;
        const std::string_view chunk(buffer, static_cast<size_t>(file.gcount()));
        const size_t newline = chunk.find('\n');
        line.append(chunk.substr(0, newline));
        if (newline != std::string_view::npos)
            break;
    }
    return line;
}

} // namespace

AgentUsageRoots AgentUsageRoots::from_environment()
{
    AgentUsageRoots roots;
    const auto home = home_directory();
    auto claude = environment_path("CLAUDE_CONFIG_DIR");
    if (claude.empty() && !home.empty())
        claude = home / ".claude";
    if (!claude.empty())
        roots.claude_projects = claude / "projects";
    auto codex = environment_path("CODEX_HOME");
    if (codex.empty() && !home.empty())
        codex = home / ".codex";
    if (!codex.empty())
        roots.codex_sessions = codex / "sessions";
    return roots;
}

// ---------------------------------------------------------------------------
// Monitor
// ---------------------------------------------------------------------------

struct AgentUsageMonitor::Impl
{
    struct FileState
    {
        explicit FileState(std::string_view session_kind)
            : kind(session_kind)
            , tail(session_kind)
        {
        }
        std::string kind;
        AgentSessionTail tail;
        TokenRateWindow rate;
        uintmax_t offset = 0;
        uintmax_t size = 0;
        bool fresh = true;
    };

    struct Binding
    {
        std::string kind;
        std::filesystem::path path;
        std::optional<AgentSessionRef> session_ref;
        bool attempted = false;
        UsageTimestamp resolved_at{};
    };

    struct Watch
    {
        std::unique_ptr<FileMonitor> monitor;
        UsageTimestamp next_attempt{};
    };

    AgentUsageRoots roots;
    AgentUsageMonitorOptions options;
    std::unordered_map<std::string, Binding> bindings;
    std::unordered_map<std::string, std::unique_ptr<FileState>> files;
    std::unordered_map<std::string, std::string> codex_cwd_cache;
    std::unordered_map<std::string, std::filesystem::path> id_cache;
    Watch claude_watch;
    Watch codex_watch;
    UsageTimestamp next_reconcile{};

    void reset()
    {
        bindings.clear();
        files.clear();
        codex_cwd_cache.clear();
        id_cache.clear();
        claude_watch = {};
        codex_watch = {};
        next_reconcile = {};
    }

    // Returns true if the root reported changes (or cannot be watched and must
    // be treated as always changed).
    bool poll_watch(Watch& watch, const std::filesystem::path& root, UsageTimestamp now)
    {
        if (!options.native_watch)
            return true;
        std::error_code error;
        if (root.empty() || !std::filesystem::is_directory(root, error))
        {
            watch.monitor.reset();
            return false;
        }
        if (watch.monitor && !watch.monitor->error().empty())
            watch.monitor.reset();
        if (!watch.monitor)
        {
            if (now < watch.next_attempt)
                return true;
            std::string create_error;
            watch.monitor = FileMonitor::create({ root }, [] {}, create_error);
            if (!watch.monitor)
            {
                watch.next_attempt = now + options.reconcile_interval;
                return true;
            }
            // Newly armed: everything since the last look is unknown.
            return true;
        }
        return watch.monitor->consume_changes();
    }

    std::filesystem::path resolve_claude(const AgentUsageRequest& request, bool ambiguous,
        const std::unordered_set<std::string>& claimed)
    {
        const std::filesystem::path& root = roots.claude_projects;
        if (root.empty())
            return {};
        const std::string cwd = normalized_directory(request.working_directory);
        if (request.session_ref)
        {
            const AgentSessionRef& ref = *request.session_ref;
            if (ref.kind == AgentSessionRefKind::Path)
                return std::filesystem::path(ref.value);
            const std::string file_name = ref.value + ".jsonl";
            std::error_code error;
            if (!cwd.empty())
            {
                const auto direct = root / claude_project_directory_name(cwd) / file_name;
                if (std::filesystem::is_regular_file(direct, error))
                    return direct;
            }
            if (const auto cached = id_cache.find(ref.value); cached != id_cache.end())
                return cached->second;
            for (std::filesystem::directory_iterator iterator(root, error), end;
                 !error && iterator != end; iterator.increment(error))
            {
                const auto candidate = iterator->path() / file_name;
                std::error_code candidate_error;
                if (std::filesystem::is_regular_file(candidate, candidate_error))
                {
                    id_cache[ref.value] = candidate;
                    return candidate;
                }
            }
            return {};
        }
        if (cwd.empty() || ambiguous)
            return {};
        const auto files_in_project = jsonl_files(root / claude_project_directory_name(cwd));
        const Candidate* newest = nullptr;
        for (const Candidate& candidate : files_in_project)
        {
            if (candidate.modified + kStartSlack < request.started_at
                || claimed.contains(candidate.path.string()))
                continue;
            if (!newest || candidate.modified > newest->modified)
                newest = &candidate;
        }
        return newest ? newest->path : std::filesystem::path{};
    }

    std::string codex_working_directory(const std::filesystem::path& path)
    {
        const std::string key = path.string();
        if (const auto cached = codex_cwd_cache.find(key); cached != codex_cwd_cache.end())
            return cached->second;
        const std::string line = read_first_line(path, kCodexMetaLineLimit);
        std::string cwd;
        const auto record = nlohmann::json::parse(line, nullptr, false);
        if (!record.is_discarded() && record.is_object() && record.value("type", "") == "session_meta")
        {
            const auto payload = record.find("payload");
            if (payload != record.end() && payload->is_object())
                cwd = payload->value("cwd", "");
        }
        codex_cwd_cache[key] = cwd;
        return cwd;
    }

    std::vector<std::filesystem::path> codex_day_directories(const AgentUsageRequest& request, UsageTimestamp now)
    {
        std::vector<std::filesystem::path> result;
        auto day = std::chrono::system_clock::time_point(now);
        const auto earliest = std::chrono::system_clock::time_point(request.started_at) - std::chrono::hours{ 24 };
        for (int index = 0; index < kCodexDayLookback && day >= earliest; ++index)
        {
            result.push_back(codex_day_directory(roots.codex_sessions, day));
            day -= std::chrono::hours{ 24 };
        }
        return result;
    }

    std::filesystem::path resolve_codex(const AgentUsageRequest& request, bool ambiguous,
        const std::unordered_set<std::string>& claimed, UsageTimestamp now)
    {
        const std::filesystem::path& root = roots.codex_sessions;
        if (root.empty())
            return {};
        if (request.session_ref)
        {
            const AgentSessionRef& ref = *request.session_ref;
            if (ref.kind == AgentSessionRefKind::Path)
                return std::filesystem::path(ref.value);
            if (const auto cached = id_cache.find(ref.value); cached != id_cache.end())
                return cached->second;
            const std::string suffix = "-" + ref.value + ".jsonl";
            const auto matches = [&](const std::filesystem::path& path) {
                return path.filename().string().ends_with(suffix);
            };
            for (const auto& directory : codex_day_directories(request, now))
            {
                for (const Candidate& candidate : jsonl_files(directory))
                {
                    if (matches(candidate.path))
                    {
                        id_cache[ref.value] = candidate.path;
                        return candidate.path;
                    }
                }
            }
            // A resumed older session: one bounded recursive search, cached.
            std::error_code error;
            for (std::filesystem::recursive_directory_iterator iterator(
                     root, std::filesystem::directory_options::skip_permission_denied, error),
                 end;
                 !error && iterator != end; iterator.increment(error))
            {
                if (matches(iterator->path()))
                {
                    id_cache[ref.value] = iterator->path();
                    return iterator->path();
                }
            }
            return {};
        }
        const std::string cwd = directory_key(request.working_directory);
        if (cwd.empty() || ambiguous)
            return {};
        std::optional<Candidate> newest;
        for (const auto& directory : codex_day_directories(request, now))
        {
            for (const Candidate& candidate : jsonl_files(directory))
            {
                if (candidate.modified + kStartSlack < request.started_at
                    || (newest && candidate.modified <= newest->modified)
                    || claimed.contains(candidate.path.string()))
                    continue;
                if (directory_key(codex_working_directory(candidate.path)) == cwd)
                    newest = candidate;
            }
        }
        return newest ? newest->path : std::filesystem::path{};
    }

    void read_file(FileState& file, const std::filesystem::path& path, UsageTimestamp now)
    {
        std::error_code error;
        const uintmax_t size = std::filesystem::file_size(path, error);
        if (error)
            return;
        if (size < file.offset)
        {
            // Truncated or replaced: start this file over.
            file.tail = AgentSessionTail(file.kind);
            file.rate = {};
            file.offset = 0;
        }
        file.size = size;
        if (size == file.offset)
            return;

        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            return;
        stream.seekg(static_cast<std::streamoff>(file.offset));
        const uintmax_t wanted = std::min<uintmax_t>(size - file.offset, options.read_budget_bytes);
        std::string bytes(static_cast<size_t>(wanted), '\0');
        stream.read(bytes.data(), static_cast<std::streamsize>(wanted));
        bytes.resize(static_cast<size_t>(std::max<std::streamsize>(0, stream.gcount())));
        file.offset += bytes.size();

        std::vector<SessionTokenDelta> deltas;
        file.tail.consume(bytes, deltas);
        std::map<UsageTimestamp, uint64_t> measurements;
        for (const SessionTokenDelta& delta : deltas)
        {
            if (delta.timestamp >= now - kRateHistory && delta.timestamp <= now + kFutureTolerance)
                measurements[delta.timestamp] += delta.tokens;
        }
        for (const auto& [timestamp, tokens] : measurements)
            file.rate.observe(timestamp, tokens);
    }
};

AgentUsageMonitor::AgentUsageMonitor(AgentUsageRoots roots, AgentUsageMonitorOptions options)
    : impl_(std::make_unique<Impl>())
{
    impl_->roots = std::move(roots);
    impl_->options = options;
}

AgentUsageMonitor::~AgentUsageMonitor() = default;

std::unordered_map<std::string, AgentActivity> AgentUsageMonitor::update(
    const std::vector<AgentUsageRequest>& requests, UsageTimestamp now)
{
    Impl& impl = *impl_;
    std::unordered_map<std::string, AgentActivity> result;
    if (requests.empty())
    {
        impl.reset();
        return result;
    }

    const bool wants_claude = std::ranges::any_of(requests, [](const auto& r) { return r.kind == "claude"; });
    const bool wants_codex = std::ranges::any_of(requests, [](const auto& r) { return r.kind == "codex"; });
    bool claude_changed = false;
    bool codex_changed = false;
    if (wants_claude)
        claude_changed = impl.poll_watch(impl.claude_watch, impl.roots.claude_projects, now);
    else
        impl.claude_watch = {};
    if (wants_codex)
        codex_changed = impl.poll_watch(impl.codex_watch, impl.roots.codex_sessions, now);
    else
        impl.codex_watch = {};
    const bool reconcile = now >= impl.next_reconcile;
    if (reconcile)
        impl.next_reconcile = now + impl.options.reconcile_interval;

    // Agents sharing a kind and directory without a native session reference
    // cannot be told apart from the files alone; leave them unattributed.
    std::unordered_map<std::string, int> unreferenced_groups;
    for (const auto& request : requests)
        if (!request.session_ref)
            ++unreferenced_groups[request.kind + '\x1f' + directory_key(request.working_directory)];

    // Resolve agents with a native session reference first: the files they
    // claim are excluded when locating unreferenced agents by directory.
    std::vector<const AgentUsageRequest*> ordered;
    for (const auto& request : requests)
        if (request.session_ref)
            ordered.push_back(&request);
    for (const auto& request : requests)
        if (!request.session_ref)
            ordered.push_back(&request);
    std::unordered_set<std::string> claimed;

    std::unordered_set<std::string> live;
    for (const AgentUsageRequest* ordered_request : ordered)
    {
        const AgentUsageRequest& request = *ordered_request;
        if (request.kind != "claude" && request.kind != "codex")
            continue;
        live.insert(request.instance_id);
        Impl::Binding& binding = impl.bindings[request.instance_id];
        if (binding.kind != request.kind)
            binding = { .kind = request.kind };
        const bool changed = request.kind == "claude" ? claude_changed : codex_changed;
        const bool reference_changed = binding.session_ref != request.session_ref;
        // Unbound agents retry on every change (their file may just have
        // appeared); bound agents re-check at most every few seconds.
        const bool due = !binding.attempted || reference_changed
            || ((changed || reconcile)
                && (binding.path.empty() || now - binding.resolved_at >= kMinimumResolveInterval));
        if (due)
        {
            const bool ambiguous = !request.session_ref
                && unreferenced_groups[request.kind + '\x1f' + directory_key(request.working_directory)] > 1;
            binding.path = request.kind == "claude"
                ? impl.resolve_claude(request, ambiguous, claimed)
                : impl.resolve_codex(request, ambiguous, claimed, now);
            binding.session_ref = request.session_ref;
            binding.attempted = true;
            binding.resolved_at = now;
        }
        if (request.session_ref && !binding.path.empty())
            claimed.insert(binding.path.string());
    }
    std::erase_if(impl.bindings, [&](const auto& entry) { return !live.contains(entry.first); });

    std::unordered_set<std::string> bound_paths;
    for (const auto& [instance_id, binding] : impl.bindings)
    {
        if (binding.path.empty())
            continue;
        const std::string key = binding.path.string();
        bound_paths.insert(key);
        auto& file = impl.files[key];
        if (!file)
            file = std::make_unique<Impl::FileState>(binding.kind);
        const bool changed = binding.kind == "claude" ? claude_changed : codex_changed;
        if (file->fresh || changed || reconcile || file->offset < file->size)
        {
            impl.read_file(*file, binding.path, now);
            file->fresh = false;
        }
        const auto measured_at = file->rate.measured_at();
        result[instance_id] = AgentActivity{
            .tokens_per_second = measured_at ? file->rate.tokens_per_second() : 0.0,
            .measured_at_ms = measured_at ? measured_at->time_since_epoch().count() : 0,
            .session_tokens = file->tail.session_tokens(),
        };
    }
    std::erase_if(impl.files, [&](const auto& entry) { return !bound_paths.contains(entry.first); });
    if (impl.codex_cwd_cache.size() > 4096)
        impl.codex_cwd_cache.clear();
    return result;
}

} // namespace draxul

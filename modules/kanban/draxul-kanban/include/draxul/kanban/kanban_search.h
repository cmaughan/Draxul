#pragma once

#include <draxul/kanban/kanban_board.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

namespace draxul::kanban
{

// Card text search. A query is split on whitespace into terms; a card matches
// when every term occurs in its file name or Markdown text. Matching folds
// ASCII letters only, so non-ASCII text must match exactly.
std::string kanban_search_fold(std::string_view text);
std::vector<std::string> kanban_search_terms(std::string_view query);
bool kanban_search_matches(const std::vector<std::string>& terms,
    std::string_view folded_name, std::string_view folded_text);

// Card identity for search matches: its file path. File names can repeat
// across lanes, so the name alone is ambiguous.
std::string kanban_search_key(const KanbanCard& card);

struct KanbanSearchResult
{
    uint64_t generation = 0;
    std::unordered_set<std::string> matches;
    size_t unreadable = 0;
};

// Searches card files off the UI thread. Each submit supersedes any pending
// or running request: the worker abandons superseded work between files and
// publishes only the latest generation. File text is cached by write time and
// size, so refining a query re-reads only cards that changed on disk.
// on_result runs on the worker thread and must be thread-safe.
class KanbanSearchWorker
{
public:
    explicit KanbanSearchWorker(std::function<void()> on_result);
    ~KanbanSearchWorker();

    KanbanSearchWorker(const KanbanSearchWorker&) = delete;
    KanbanSearchWorker& operator=(const KanbanSearchWorker&) = delete;

    void submit(uint64_t generation, std::string query, std::vector<KanbanCard> cards);
    std::optional<KanbanSearchResult> take_result();

private:
    struct Request
    {
        uint64_t generation = 0;
        std::string query;
        std::vector<KanbanCard> cards;
    };
    struct CachedText
    {
        std::filesystem::file_time_type write_time;
        uintmax_t size = 0;
        std::string folded;
    };

    void run();
    std::optional<KanbanSearchResult> search(const Request& request);
    const std::string* cached_text(const std::filesystem::path& path);

    std::function<void()> on_result_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_ = false;
    std::optional<Request> pending_;
    std::optional<KanbanSearchResult> result_;
    std::atomic<uint64_t> latest_generation_{ 0 };
    // Worker-thread only.
    std::map<std::filesystem::path, CachedText> cache_;
    std::thread thread_;
};

} // namespace draxul::kanban

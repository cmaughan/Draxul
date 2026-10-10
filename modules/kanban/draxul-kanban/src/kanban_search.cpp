#include <draxul/kanban/kanban_search.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace draxul::kanban
{
namespace
{

// Cards are short Markdown notes; bound a pathological file so one huge
// document cannot stall every keystroke.
constexpr std::streamsize kMaxCardBytes = 4 * 1024 * 1024;

bool is_search_space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
}

} // namespace

std::string kanban_search_fold(std::string_view text)
{
    std::string folded(text);
    for (char& ch : folded)
    {
        if (ch >= 'A' && ch <= 'Z')
            ch = static_cast<char>(ch - 'A' + 'a');
    }
    return folded;
}

std::vector<std::string> kanban_search_terms(std::string_view query)
{
    std::vector<std::string> terms;
    size_t start = 0;
    while (start < query.size())
    {
        while (start < query.size() && is_search_space(query[start]))
            ++start;
        size_t end = start;
        while (end < query.size() && !is_search_space(query[end]))
            ++end;
        if (end > start)
            terms.push_back(kanban_search_fold(query.substr(start, end - start)));
        start = end;
    }
    return terms;
}

bool kanban_search_matches(const std::vector<std::string>& terms,
    std::string_view folded_name, std::string_view folded_text)
{
    for (const auto& term : terms)
    {
        if (folded_name.find(term) == std::string_view::npos
            && folded_text.find(term) == std::string_view::npos)
            return false;
    }
    return true;
}

std::string kanban_search_key(const KanbanCard& card)
{
    return kanban_path_utf8(card.path);
}

KanbanSearchWorker::KanbanSearchWorker(std::function<void()> on_result)
    : on_result_(std::move(on_result))
    , thread_([this] { run(); })
{
}

KanbanSearchWorker::~KanbanSearchWorker()
{
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
        pending_.reset();
    }
    // Wakes the worker and makes any in-flight search abandon at its next file.
    latest_generation_.fetch_add(1);
    wake_.notify_all();
    if (thread_.joinable())
        thread_.join();
}

void KanbanSearchWorker::submit(uint64_t generation, std::string query, std::vector<KanbanCard> cards)
{
    {
        std::lock_guard lock(mutex_);
        latest_generation_.store(generation);
        pending_ = Request{
            .generation = generation,
            .query = std::move(query),
            .cards = std::move(cards),
        };
        result_.reset();
    }
    wake_.notify_one();
}

std::optional<KanbanSearchResult> KanbanSearchWorker::take_result()
{
    std::lock_guard lock(mutex_);
    std::optional<KanbanSearchResult> result = std::move(result_);
    result_.reset();
    return result;
}

void KanbanSearchWorker::run()
{
    for (;;)
    {
        Request request;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return stop_ || pending_.has_value(); });
            if (stop_)
                return;
            request = std::move(*pending_);
            pending_.reset();
        }

        auto result = search(request);
        if (!result)
            continue;
        {
            std::lock_guard lock(mutex_);
            if (stop_ || pending_ || latest_generation_.load() != result->generation)
                continue;
            result_ = std::move(result);
        }
        if (on_result_)
            on_result_();
    }
}

std::optional<KanbanSearchResult> KanbanSearchWorker::search(const Request& request)
{
    const auto terms = kanban_search_terms(request.query);
    KanbanSearchResult result;
    result.generation = request.generation;
    if (cache_.size() > request.cards.size() * 4 + 256)
        cache_.clear();

    for (const auto& card : request.cards)
    {
        if (latest_generation_.load() != request.generation)
            return std::nullopt;
        const std::string* text = cached_text(card.path);
        if (!text)
        {
            ++result.unreadable;
            continue;
        }
        if (kanban_search_matches(terms, kanban_search_fold(card.file_name), *text))
            result.matches.insert(kanban_search_key(card));
    }
    return result;
}

const std::string* KanbanSearchWorker::cached_text(const std::filesystem::path& path)
{
    std::error_code error;
    const auto write_time = std::filesystem::last_write_time(path, error);
    if (error)
        return nullptr;
    const auto size = std::filesystem::file_size(path, error);
    if (error)
        return nullptr;

    if (const auto found = cache_.find(path);
        found != cache_.end() && found->second.write_time == write_time && found->second.size == size)
        return &found->second.folded;

    std::ifstream input(path, std::ios::binary);
    if (!input)
        return nullptr;
    std::string text(static_cast<size_t>(std::min<uintmax_t>(size, kMaxCardBytes)), '\0');
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<size_t>(std::max<std::streamsize>(0, input.gcount())));

    auto& entry = cache_[path];
    entry.write_time = write_time;
    entry.size = size;
    entry.folded = kanban_search_fold(text);
    return &entry.folded;
}

} // namespace draxul::kanban

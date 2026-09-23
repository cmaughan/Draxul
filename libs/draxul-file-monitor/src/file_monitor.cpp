#include "file_monitor_internal.h"

#include <algorithm>
#include <utility>

namespace draxul
{
FileMonitor::FileMonitor(Wake wake)
    : wake_(std::move(wake))
{
}

std::unique_ptr<FileMonitor> FileMonitor::create(
    const std::vector<std::filesystem::path>& roots, Wake wake, std::string& error)
{
    error.clear();
    std::vector<std::filesystem::path> normalized;
    for (const auto& root : roots)
    {
        std::error_code ec;
        auto path = std::filesystem::canonical(root, ec);
        if (ec || !std::filesystem::is_directory(path, ec))
        {
            error = "Cannot monitor directory " + root.string() + ": "
                + (ec ? ec.message() : "not a directory");
            return nullptr;
        }
        if (std::find(normalized.begin(), normalized.end(), path) == normalized.end())
            normalized.push_back(std::move(path));
    }
    if (normalized.empty())
    {
        error = "File monitor requires at least one directory";
        return nullptr;
    }
    return make_native_file_monitor(normalized, std::move(wake), error);
}

bool FileMonitor::consume_changes()
{
    return changed_.exchange(false);
}

std::string FileMonitor::error() const
{
    std::lock_guard lock(error_mutex_);
    return error_;
}

void FileMonitor::changed()
{
    // One wake per outstanding invalidation. Consumers drain on their own thread.
    if (!changed_.exchange(true) && wake_)
        wake_();
}

void FileMonitor::failed(std::string message)
{
    {
        std::lock_guard lock(error_mutex_);
        error_ = std::move(message);
    }
    changed();
}
} // namespace draxul

#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace draxul
{

// Native, recursive directory invalidation, not an exact filesystem event log.
// A notification (including native event overflow) means the consumer must rescan.
// Roots must exist and remain at their original paths; after a terminal error,
// recreate the monitor. No backend silently falls back to polling.
class FileMonitor
{
public:
    using Wake = std::function<void()>;

    // Armed before returning. Wake runs on a worker and must be thread-safe,
    // nonblocking, nonthrowing, and must not destroy/reconfigure this monitor. Destruction
    // stops notifications and joins/drains callbacks before returning.
    static std::unique_ptr<FileMonitor> create(
        const std::vector<std::filesystem::path>& roots, Wake wake, std::string& error);
    virtual ~FileMonitor() = default;
    FileMonitor(const FileMonitor&) = delete;
    FileMonitor& operator=(const FileMonitor&) = delete;

    bool consume_changes();
    // Empty while healthy; a terminal backend error also wakes the consumer.
    std::string error() const;

protected:
    explicit FileMonitor(Wake wake);
    void changed();
    void failed(std::string message);

private:
    Wake wake_;
    std::atomic<bool> changed_{ false };
    mutable std::mutex error_mutex_;
    std::string error_;
};

} // namespace draxul

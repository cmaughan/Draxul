#include "file_monitor_internal.h"

#include <array>
#include <cerrno>
#include <map>
#include <poll.h>
#include <set>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <utility>

namespace draxul
{
namespace
{
class LinuxFileMonitor final : public FileMonitor
{
public:
    explicit LinuxFileMonitor(Wake wake) : FileMonitor(std::move(wake)) {}
    ~LinuxFileMonitor() override
    {
        if (stop_ >= 0)
        {
            uint64_t one = 1;
            (void)write(stop_, &one, sizeof(one));
        }
        if (worker_.joinable())
            worker_.join();
        if (notify_ >= 0)
            close(notify_);
        if (stop_ >= 0)
            close(stop_);
    }

    bool start(const std::vector<std::filesystem::path>& roots, std::string& error)
    {
        roots_ = roots;
        notify_ = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
        stop_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (notify_ < 0 || stop_ < 0)
        {
            error = std::generic_category().message(errno);
            return false;
        }
        if (!sync_directories(error))
            return false;
        worker_ = std::thread([this] { run(); });
        return true;
    }

private:
    bool sync_directories(std::string& error)
    {
        std::set<std::filesystem::path> directories;
        const auto add = [&](const std::filesystem::path& path) {
            directories.insert(path);
            for (const auto& [wd, watched] : watches_)
                if (watched == path)
                    return true;
            const int wd = inotify_add_watch(notify_, path.c_str(),
                IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_MODIFY
                    | IN_ATTRIB | IN_CLOSE_WRITE | IN_DELETE_SELF | IN_MOVE_SELF
                    | IN_ONLYDIR | IN_DONT_FOLLOW);
            if (wd < 0)
            {
                error = "Cannot watch " + path.string() + ": "
                    + std::generic_category().message(errno);
                return false;
            }
            watches_[wd] = path;
            return true;
        };
        for (const auto& root : roots_)
        {
            if (!add(root))
                return false;
            std::error_code ec;
            std::filesystem::recursive_directory_iterator it(root, ec), end;
            for (; !ec && it != end; it.increment(ec))
            {
                if (it->is_directory(ec) && !it->is_symlink(ec) && !add(it->path()))
                    return false;
            }
            if (ec)
            {
                error = "Cannot enumerate watched directory: " + ec.message();
                return false;
            }
        }
        for (auto it = watches_.begin(); it != watches_.end();)
        {
            if (!directories.contains(it->second))
            {
                inotify_rm_watch(notify_, it->first);
                it = watches_.erase(it);
            }
            else
                ++it;
        }
        return true;
    }

    void run()
    {
        pollfd fds[]{ { stop_, POLLIN, 0 }, { notify_, POLLIN, 0 } };
        alignas(inotify_event) std::array<char, 64 * 1024> buffer{};
        for (;;)
        {
            const int ready = poll(fds, 2, -1);
            if (ready < 0 && errno == EINTR)
                continue;
            if (ready < 0 || (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)))
            {
                failed("File notification wait failed");
                return;
            }
            if (fds[0].revents)
                return;
            bool dirty = false;
            bool structure_changed = false;
            bool overflow = false;
            // Bound each drain so a busy writer cannot starve shutdown.
            for (int batch = 0; batch < 16; ++batch)
            {
                const auto bytes = read(notify_, buffer.data(), buffer.size());
                if (bytes < 0 && errno == EINTR)
                    continue;
                if (bytes < 0 && errno == EAGAIN)
                    break;
                if (bytes <= 0)
                {
                    failed("Cannot read file notifications");
                    return;
                }
                for (size_t offset = 0; offset < static_cast<size_t>(bytes);)
                {
                    const auto& event = *reinterpret_cast<const inotify_event*>(buffer.data() + offset);
                    offset += sizeof(inotify_event) + event.len;
                    if (event.mask & (IN_MOVE_SELF | IN_DELETE_SELF | IN_UNMOUNT))
                    {
                        const auto found = watches_.find(event.wd);
                        if (found != watches_.end())
                            for (const auto& root : roots_)
                                if (root == found->second)
                                {
                                    failed("Watched directory moved or disappeared; reload to rearm");
                                    return;
                                }
                    }
                    if (event.mask & IN_IGNORED)
                        watches_.erase(event.wd);
                    else
                        dirty = true;
                    structure_changed |= (event.mask & (IN_ISDIR | IN_Q_OVERFLOW)) != 0;
                    overflow |= (event.mask & IN_Q_OVERFLOW) != 0;
                }
            }
            if (structure_changed)
            {
                if (overflow)
                {
                    // Lost IN_IGNORED/rename events make cached watch identities
                    // unreliable. Rebuild the native subscription, not just paths.
                    close(notify_);
                    notify_ = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
                    fds[1].fd = notify_;
                    watches_.clear();
                    if (notify_ < 0)
                    {
                        failed("Cannot restart file notifications after overflow");
                        return;
                    }
                }
                std::string error;
                if (!sync_directories(error))
                {
                    failed(std::move(error));
                    return;
                }
            }
            if (dirty)
                changed();
        }
    }

    int notify_ = -1;
    int stop_ = -1;
    std::vector<std::filesystem::path> roots_;
    std::map<int, std::filesystem::path> watches_;
    std::thread worker_;
};
}

std::unique_ptr<FileMonitor> make_native_file_monitor(
    const std::vector<std::filesystem::path>& roots, FileMonitor::Wake wake, std::string& error)
{
    auto monitor = std::make_unique<LinuxFileMonitor>(std::move(wake));
    if (!monitor->start(roots, error))
        return nullptr;
    return monitor;
}
} // namespace draxul

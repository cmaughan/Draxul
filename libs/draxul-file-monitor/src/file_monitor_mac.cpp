#include "file_monitor_internal.h"

#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>
#include <utility>

namespace draxul
{
namespace
{
class MacFileMonitor final : public FileMonitor
{
public:
    explicit MacFileMonitor(Wake wake) : FileMonitor(std::move(wake)) {}

    ~MacFileMonitor() override
    {
        if (stream_)
        {
            if (started_)
                FSEventStreamStop(stream_);
            FSEventStreamInvalidate(stream_);
            // Invalidation prevents new callbacks; drain any already queued.
            dispatch_sync_f(queue_, nullptr, [](void*) {});
            FSEventStreamRelease(stream_);
        }
        if (queue_)
            dispatch_release(queue_);
    }

    bool start(const std::vector<std::filesystem::path>& roots, std::string& error)
    {
        auto paths = CFArrayCreateMutable(nullptr, 0, &kCFTypeArrayCallBacks);
        for (const auto& root : roots)
        {
            auto path = CFStringCreateWithFileSystemRepresentation(nullptr, root.c_str());
            if (!path)
            {
                CFRelease(paths);
                error = "Cannot encode file monitor path";
                return false;
            }
            CFArrayAppendValue(paths, path);
            CFRelease(path);
        }
        FSEventStreamContext context{ 0, this, nullptr, nullptr, nullptr };
        stream_ = FSEventStreamCreate(nullptr, &receive, &context, paths,
            kFSEventStreamEventIdSinceNow, 0.05,
            kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagWatchRoot
                | kFSEventStreamCreateFlagNoDefer);
        CFRelease(paths);
        if (!stream_)
        {
            error = "Cannot create FSEvents stream";
            return false;
        }
        queue_ = dispatch_queue_create("draxul.file-monitor", DISPATCH_QUEUE_SERIAL);
        FSEventStreamSetDispatchQueue(stream_, queue_);
        started_ = FSEventStreamStart(stream_);
        if (!started_)
            error = "Cannot start FSEvents stream";
        return started_;
    }

private:
    static void receive(ConstFSEventStreamRef, void* context, size_t count,
        void*, const FSEventStreamEventFlags flags[], const FSEventStreamEventId[])
    {
        auto& self = *static_cast<MacFileMonitor*>(context);
        for (size_t i = 0; i < count; ++i)
        {
            if (flags[i] & (kFSEventStreamEventFlagRootChanged | kFSEventStreamEventFlagUnmount))
            {
                self.failed("Watched directory moved, disappeared, or was unmounted; reload to rearm");
                return;
            }
        }
        // Dropped/MustScanSubDirs events also invalidate the entire snapshot.
        if (count)
            self.changed();
    }

    FSEventStreamRef stream_ = nullptr;
    dispatch_queue_t queue_ = nullptr;
    bool started_ = false;
};
}

std::unique_ptr<FileMonitor> make_native_file_monitor(
    const std::vector<std::filesystem::path>& roots, FileMonitor::Wake wake, std::string& error)
{
    auto monitor = std::make_unique<MacFileMonitor>(std::move(wake));
    if (!monitor->start(roots, error))
        return nullptr;
    return monitor;
}
} // namespace draxul

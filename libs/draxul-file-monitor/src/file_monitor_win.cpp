#include "file_monitor_internal.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <system_error>
#include <thread>
#include <utility>

namespace draxul
{
namespace
{
std::string windows_error()
{
    return std::system_category().message(static_cast<int>(GetLastError()));
}

// We need invalidation, not individual filenames. Change-notification handles
// avoid outstanding overlapped buffers and permit immediate, explicit shutdown.
class WindowsFileMonitor final : public FileMonitor
{
public:
    explicit WindowsFileMonitor(Wake wake) : FileMonitor(std::move(wake)) {}
    ~WindowsFileMonitor() override
    {
        if (stop_)
            SetEvent(stop_);
        if (worker_.joinable())
            worker_.join();
        for (auto handle : watches_)
            FindCloseChangeNotification(handle);
        if (stop_)
            CloseHandle(stop_);
    }

    bool start(const std::vector<std::filesystem::path>& roots, std::string& error)
    {
        if (roots.size() >= MAXIMUM_WAIT_OBJECTS)
        {
            error = "File monitor supports at most 63 roots per instance on Windows";
            return false;
        }
        stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!stop_)
        {
            error = windows_error();
            return false;
        }
        for (const auto& root : roots)
        {
            auto handle = FindFirstChangeNotificationW(root.c_str(), TRUE,
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME
                    | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE
                    | FILE_NOTIFY_CHANGE_ATTRIBUTES);
            if (handle == INVALID_HANDLE_VALUE)
            {
                error = "Cannot monitor " + root.string() + ": " + windows_error();
                return false;
            }
            watches_.push_back(handle);
        }
        worker_ = std::thread([this] {
            std::vector<HANDLE> handles{ stop_ };
            handles.insert(handles.end(), watches_.begin(), watches_.end());
            for (;;)
            {
                const DWORD result = WaitForMultipleObjects(
                    static_cast<DWORD>(handles.size()), handles.data(), FALSE, INFINITE);
                if (result == WAIT_OBJECT_0)
                    return;
                if (result == WAIT_FAILED)
                {
                    failed("File notification wait failed: " + windows_error());
                    return;
                }
                const auto index = result - WAIT_OBJECT_0;
                if (index >= handles.size() || !FindNextChangeNotification(handles[index]))
                {
                    failed("Cannot rearm file notification: " + windows_error());
                    return;
                }
                changed();
            }
        });
        return true;
    }

private:
    HANDLE stop_ = nullptr;
    std::vector<HANDLE> watches_;
    std::thread worker_;
};
}

std::unique_ptr<FileMonitor> make_native_file_monitor(
    const std::vector<std::filesystem::path>& roots, FileMonitor::Wake wake, std::string& error)
{
    auto monitor = std::make_unique<WindowsFileMonitor>(std::move(wake));
    if (!monitor->start(roots, error))
        return nullptr;
    return monitor;
}
} // namespace draxul

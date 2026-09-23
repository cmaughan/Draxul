#include "file_monitor_internal.h"

namespace draxul
{
std::unique_ptr<FileMonitor> make_native_file_monitor(
    const std::vector<std::filesystem::path>&, FileMonitor::Wake, std::string& error)
{
    error = "Native file monitoring is not supported on this platform";
    return nullptr;
}
}

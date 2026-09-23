#pragma once
#include <draxul/file_monitor.h>

namespace draxul
{
std::unique_ptr<FileMonitor> make_native_file_monitor(
    const std::vector<std::filesystem::path>& roots, FileMonitor::Wake wake,
    std::string& error);
}

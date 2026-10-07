#pragma once
#include <filesystem>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#endif
namespace draxul
{
class CollectionLock
{
public:
    CollectionLock() = default;
    CollectionLock(const CollectionLock&) = delete;
    CollectionLock& operator=(const CollectionLock&) = delete;
    ~CollectionLock()
    {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
#else
        if (fd_ >= 0) { flock(fd_, LOCK_UN); close(fd_); }
#endif
    }
    void acquire(const std::filesystem::path& path)
    {
        std::filesystem::create_directories(path.parent_path());
#ifdef _WIN32
        handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE)
            throw std::runtime_error("Another local server owns this collection, or its lock is unavailable.");
#else
        fd_ = open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (fd_ < 0 || flock(fd_, LOCK_EX | LOCK_NB) != 0)
            throw std::runtime_error("Another local server owns this collection, or its lock is unavailable.");
#endif
    }
private:
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int fd_ = -1;
#endif
};
}

#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace draxul::tests
{

struct TempDir
{
    std::filesystem::path path;

    explicit TempDir(const char* prefix)
    {
        static std::atomic<uint64_t> counter = 0;
#ifdef _WIN32
        const std::filesystem::path base = std::filesystem::temp_directory_path();
        const auto suffix = std::to_string(
                                std::chrono::steady_clock::now().time_since_epoch().count())
            + "-" + std::to_string(counter++);
#else
        // sockaddr_un::sun_path is 104 bytes. macOS's per-user $TMPDIR
        // (/var/folders/<..>/T/) is ~49 of them before a test nests a
        // redirected HOME — and "Library/Application Support/draxul/runtime"
        // adds 44 more — so a unix socket created under a TempDir overflows
        // bind(). Keep the base shallow and the unique part short. The pid
        // keeps parallel CTest shards from colliding.
        const std::filesystem::path base = "/tmp";
        const auto suffix = std::to_string(static_cast<long long>(::getpid())) + "-" + std::to_string(counter++);
#endif
        path = base / (std::string(prefix) + "-" + suffix);
        std::filesystem::create_directories(path);
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

} // namespace draxul::tests

#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#ifndef HPCON
using HPCON = HANDLE;
#endif

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
extern "C" HRESULT WINAPI CreatePseudoConsole(COORD size, HANDLE input, HANDLE output, DWORD flags, HPCON* pty);
extern "C" HRESULT WINAPI ResizePseudoConsole(HPCON pty, COORD size);
extern "C" void WINAPI ClosePseudoConsole(HPCON pty);
#endif

namespace draxul
{

// Spawns a child process inside a Windows Pseudo Console (ConPty) and
// provides read/write access to it via a background reader thread.
// Used by PowerShellHost and ShellHost on Windows.
class ConPtyProcess
{
public:
    ~ConPtyProcess();

    bool spawn(const std::string& command, const std::vector<std::string>& args,
        const std::string& working_dir, int initial_cols, int initial_rows,
        std::function<void()> on_output_available);
    void shutdown();
    bool is_running() const;
    bool resize(int cols, int rows);
    bool write(std::string_view text);
    std::vector<std::string> drain_output();

private:
    void reader_main();

    HANDLE input_write_ = INVALID_HANDLE_VALUE;
    HANDLE output_read_ = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION proc_info_ = {};
    HPCON pty_ = nullptr;
    std::vector<unsigned char> attribute_storage_;
    std::thread reader_thread_;
    std::atomic<bool> reader_running_{ false };
    std::mutex output_mutex_;
    std::vector<std::string> output_chunks_;
    std::function<void()> on_output_available_;
};

} // namespace draxul

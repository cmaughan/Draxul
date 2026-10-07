#include "personal_chat_process.h"
#include <draxul/process_util.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace draxul
{
#ifdef _WIN32
namespace
{
std::wstring wide(std::string_view text)
{
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!size && !text.empty()) throw std::runtime_error("Invalid UTF-8 in provider launch.");
    std::wstring value(size, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), value.data(), size);
    return value;
}
void close_handle(HANDLE& h) { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); h = INVALID_HANDLE_VALUE; }
std::string drain(HANDLE h)
{
    std::string result;
    char data[8192];
    DWORD available = 0, count = 0;
    while (result.size() < 65536 && PeekNamedPipe(h, nullptr, 0, nullptr, &available, nullptr) && available)
    {
        if (!ReadFile(h, data, std::min<DWORD>(available, sizeof(data)), &count, nullptr) || !count) break;
        result.append(data, count);
    }
    return result;
}
}
struct PersonalChatProcess::Impl
{
    HANDLE input = INVALID_HANDLE_VALUE, output = INVALID_HANDLE_VALUE, error = INVALID_HANDLE_VALUE;
    HANDLE process = INVALID_HANDLE_VALUE, job = INVALID_HANDLE_VALUE;
    ~Impl()
    {
        // Kill the owned process tree before closing pipes. Never wait for a provider on the UI thread.
        close_handle(job);
        close_handle(process); close_handle(input); close_handle(output); close_handle(error);
    }
};
void PersonalChatProcess::start(const std::string& executable, const std::vector<std::string>& args, const std::filesystem::path& directory)
{
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE child_input = INVALID_HANDLE_VALUE, child_output = INVALID_HANDLE_VALUE, child_error = INVALID_HANDLE_VALUE;
    struct Children { HANDLE& a; HANDLE& b; HANDLE& c; ~Children(){close_handle(a);close_handle(b);close_handle(c);} } cleanup{child_input,child_output,child_error};
    static std::atomic<unsigned> serial{0};
    const auto pipe_name = L"\\\\.\\pipe\\draxul-chat-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++serial);
    impl_->input = CreateNamedPipeW(pipe_name.c_str(), PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_WAIT, 1, 65536, 65536, 0, nullptr);
    child_input = CreateFileW(pipe_name.c_str(), GENERIC_READ, 0, &sa, OPEN_EXISTING, 0, nullptr);
    if (impl_->input == INVALID_HANDLE_VALUE || child_input == INVALID_HANDLE_VALUE
        || !CreatePipe(&impl_->output, &child_output, &sa, 0)
        || !CreatePipe(&impl_->error, &child_error, &sa, 0))
        throw std::runtime_error("Cannot create provider pipes.");
    OVERLAPPED connection{};
    connection.hEvent=CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if(!connection.hEvent) throw std::runtime_error("Cannot create provider pipe connection event.");
    if(!ConnectNamedPipe(impl_->input,&connection))
    {
        const DWORD error=GetLastError();
        if(error!=ERROR_PIPE_CONNECTED)
        {
            DWORD transferred=0;
            const bool connected=error==ERROR_IO_PENDING && WaitForSingleObject(connection.hEvent,5000)==WAIT_OBJECT_0
                && GetOverlappedResult(impl_->input,&connection,&transferred,FALSE);
            if(!connected)
            {
                if(error==ERROR_IO_PENDING)
                {
                    CancelIoEx(impl_->input,&connection);
                    GetOverlappedResult(impl_->input,&connection,&transferred,TRUE);
                }
                CloseHandle(connection.hEvent);
                throw std::runtime_error("Provider pipe connection failed.");
            }
        }
    }
    CloseHandle(connection.hEvent);
    SetHandleInformation(impl_->output, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(impl_->error, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = child_input; startup.StartupInfo.hStdOutput = child_output; startup.StartupInfo.hStdError = child_error;
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<unsigned char> attributes(bytes);
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &bytes)) throw std::runtime_error("Cannot configure provider launch.");
    struct AttributeCleanup { LPPROC_THREAD_ATTRIBUTE_LIST value; ~AttributeCleanup(){DeleteProcThreadAttributeList(value);} } attribute_cleanup{startup.lpAttributeList};
    HANDLE inherited[]{child_input, child_output, child_error};
    if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr))
        throw std::runtime_error("Cannot restrict provider handles.");
    std::wstring command = quote_windows_arg(std::wstring_view(wide(executable)));
    for (const auto& arg : args) command += L" " + quote_windows_arg(std::wstring_view(wide(arg)));
    impl_->job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!impl_->job || !SetInformationJobObject(impl_->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        throw std::runtime_error("Cannot create provider process job.");
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr,
            directory.c_str(), &startup.StartupInfo, &info))
        throw std::runtime_error("Cannot start provider (Windows error " + std::to_string(GetLastError()) + ").");
    impl_->process = info.hProcess;
    if (!AssignProcessToJobObject(impl_->job, info.hProcess))
    { TerminateProcess(info.hProcess, 1); CloseHandle(info.hThread); throw std::runtime_error("Cannot own provider process tree."); }
    const DWORD resumed=ResumeThread(info.hThread); CloseHandle(info.hThread);
    if(resumed==static_cast<DWORD>(-1)) throw std::runtime_error("Cannot resume provider process.");
}
void PersonalChatProcess::write(std::string_view text, std::stop_token stop)
{
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!event) throw std::runtime_error("Cannot create provider IO event.");
    struct Cleanup { HANDLE& h; ~Cleanup(){close_handle(h);} } cleanup{event};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!text.empty())
    {
        OVERLAPPED operation{}; operation.hEvent = event; ResetEvent(event);
        DWORD written = 0;
        const bool complete = WriteFile(impl_->input, text.data(), static_cast<DWORD>(std::min<size_t>(text.size(), 65536)), &written, &operation);
        if (!complete)
        {
            if (GetLastError() != ERROR_IO_PENDING) throw std::runtime_error("Provider input pipe closed.");
            while (WaitForSingleObject(event, 20) == WAIT_TIMEOUT)
                if (stop.stop_requested() || std::chrono::steady_clock::now() >= deadline)
                {
                    CancelIoEx(impl_->input, &operation);
                    GetOverlappedResult(impl_->input, &operation, &written, TRUE);
                    throw std::runtime_error("Provider input timed out.");
                }
            if (!GetOverlappedResult(impl_->input, &operation, &written, FALSE)) throw std::runtime_error("Provider input failed.");
        }
        if (!written) throw std::runtime_error("Provider input pipe closed.");
        text.remove_prefix(written);
    }
}
std::string PersonalChatProcess::read(){return drain(impl_->output);}
std::string PersonalChatProcess::diagnostics(){return drain(impl_->error);}
bool PersonalChatProcess::running(){return WaitForSingleObject(impl_->process,0)==WAIT_TIMEOUT;}
#else
struct PersonalChatProcess::Impl
{
    int input=-1,output=-1,error=-1;
    pid_t pid=-1;
    ~Impl()
    {
        if (pid>0) { kill(-pid,SIGKILL); while(waitpid(pid,nullptr,0)<0 && errno==EINTR){} }
        for (int fd : {input,output,error}) if(fd>=0) close(fd);
    }
};
void PersonalChatProcess::start(const std::string& executable, const std::vector<std::string>& args, const std::filesystem::path& directory)
{
    int pipes[3][2]{{-1,-1},{-1,-1},{-1,-1}};
    struct Cleanup { int (&pipes)[3][2]; ~Cleanup(){for(auto& pair:pipes) for(int fd:pair) if(fd>=0) close(fd);} } cleanup{pipes};
    for(auto& pair:pipes) if(pipe(pair)<0) throw std::runtime_error("Cannot create provider pipes.");
    auto paths = resolve_exec_paths(executable);
    std::vector<std::string> storage{executable}; storage.insert(storage.end(),args.begin(),args.end());
    std::vector<char*> argv; for(auto& arg:storage) argv.push_back(arg.data()); argv.push_back(nullptr);
    const auto cwd=directory.string();
    const long open_max=sysconf(_SC_OPEN_MAX);
    const int limit=static_cast<int>(open_max>0?open_max:1024);
    impl_->pid=fork();
    if(impl_->pid<0) throw std::runtime_error("Cannot fork provider.");
    if(impl_->pid==0)
    {
        setpgid(0,0);
        if(dup2(pipes[0][0],0)<0 || dup2(pipes[1][1],1)<0 || dup2(pipes[2][1],2)<0) _exit(126);
        for(int fd=3;fd<limit;++fd) close(fd);
        signal(SIGPIPE,SIG_DFL);
        if(chdir(cwd.c_str())<0) _exit(126);
        for(const auto& path:paths) execv(path.c_str(),argv.data());
        static constexpr char error[]="Cannot execute Codex app-server. Check the configured executable and PATH.\n";
        (void)!::write(2,error,sizeof(error)-1); _exit(127);
    }
    setpgid(impl_->pid,impl_->pid);
    impl_->input=pipes[0][1]; pipes[0][1]=-1;
    impl_->output=pipes[1][0]; pipes[1][0]=-1;
    impl_->error=pipes[2][0]; pipes[2][0]=-1;
    for(int fd:{impl_->input,impl_->output,impl_->error})
        if(fcntl(fd,F_SETFL,fcntl(fd,F_GETFL)|O_NONBLOCK)<0 || fcntl(fd,F_SETFD,FD_CLOEXEC)<0)
            throw std::runtime_error("Cannot configure provider pipes.");
#ifdef F_SETNOSIGPIPE
    if(fcntl(impl_->input,F_SETNOSIGPIPE,1)<0) throw std::runtime_error("Cannot configure provider pipe signals.");
#endif
}
void PersonalChatProcess::write(std::string_view text,std::stop_token stop)
{
    // This worker owns all writes. On platforms without F_SETNOSIGPIPE, block
    // pipe signals on the worker for its remaining lifetime.
#ifndef F_SETNOSIGPIPE
    sigset_t signals; sigemptyset(&signals); sigaddset(&signals,SIGPIPE);
    pthread_sigmask(SIG_BLOCK,&signals,nullptr);
#endif
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!text.empty())
    {
        if(stop.stop_requested() || std::chrono::steady_clock::now()>=deadline) throw std::runtime_error("Provider input timed out.");
        const auto count=::write(impl_->input,text.data(),text.size());
        if(count>0) text.remove_prefix(static_cast<size_t>(count));
        else if(count<0 && (errno==EAGAIN || errno==EINTR)) { pollfd p{impl_->input,POLLOUT,0}; poll(&p,1,20); }
        else throw std::runtime_error("Provider input pipe closed.");
    }
}
namespace { std::string drain(int fd)
{
    std::string result; char data[8192];
    while(result.size()<65536) { const auto n=::read(fd,data,sizeof(data)); if(n<=0) break; result.append(data,static_cast<size_t>(n)); }
    return result;
} }
std::string PersonalChatProcess::read(){return drain(impl_->output);}
std::string PersonalChatProcess::diagnostics(){return drain(impl_->error);}
bool PersonalChatProcess::running()
{
    if(impl_->pid<=0) return false;
    const auto result=waitpid(impl_->pid,nullptr,WNOHANG);
    if(result==0 || (result<0 && errno==EINTR)) return true;
    // Reap the leader only here; close any descendants which inherited its pipes.
    kill(-impl_->pid,SIGKILL); impl_->pid=-1; return false;
}
#endif
PersonalChatProcess::PersonalChatProcess():impl_(std::make_unique<Impl>()){}
PersonalChatProcess::~PersonalChatProcess()=default;
}

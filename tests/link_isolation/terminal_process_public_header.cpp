#ifdef _WIN32
#include <draxul/conpty_process.h>
using TerminalProcess = draxul::ConPtyProcess;
#else
#include <draxul/unix_pty_process.h>
using TerminalProcess = draxul::UnixPtyProcess;
#endif

int main()
{
    TerminalProcess process;
    return process.is_running() ? 0 : 0;
}

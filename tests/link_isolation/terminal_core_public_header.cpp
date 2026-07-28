#include <draxul/terminal_core.h>
#include <draxul/terminal_identity.h>
#include <draxul/terminal_snapshot.h>

int main()
{
    const draxul::TerminalId id{ 1 };
    return id.valid() ? 0 : 1;
}

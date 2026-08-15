#include "mic_permission.h"

namespace draxul
{
namespace scoreview
{

// Non-Apple platforms have no TCC consent flow; device-open failures are
// reported by SDL itself.
MicPermission query_mic_permission()
{
    return MicPermission::Granted;
}

} // namespace scoreview
} // namespace draxul

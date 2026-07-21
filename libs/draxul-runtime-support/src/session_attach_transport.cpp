#include "session_attach_internal.h"

#include <draxul/config_document.h>

#include <cstdint>
#include <sstream>

namespace draxul::session_attach_detail
{

std::string endpoint_suffix(std::string_view session_id)
{
    uint64_t hash = 14695981039346656037ull;
    const std::string key
        = ConfigDocument::default_path().parent_path().string() + "|" + std::string(session_id);
    for (unsigned char ch : key)
    {
        hash ^= static_cast<uint64_t>(ch);
        hash *= 1099511628211ull;
    }

    std::ostringstream out;
    out << std::hex << hash;
    return out.str();
}

} // namespace draxul::session_attach_detail

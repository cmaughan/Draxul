#include <draxul/scoreview/progress_store.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>

namespace draxul
{
namespace scoreview
{

std::string piece_hash(std::string_view source_bytes)
{
    uint64_t hash = 1469598103934665603ull; // FNV-1a offset basis
    for (const char byte : source_bytes)
    {
        hash ^= static_cast<unsigned char>(byte);
        hash *= 1099511628211ull; // FNV-1a prime
    }
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(hash));
    return buffer;
}

std::filesystem::path progress_path(
    const std::filesystem::path& progress_dir, std::string_view source_bytes)
{
    return progress_dir / (piece_hash(source_bytes) + ".json");
}

std::string load_progress(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return {};
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

bool save_progress_atomic(
    const std::filesystem::path& path, const std::string& json_text, std::string& error)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
    {
        error = "could not create " + path.parent_path().string() + ": " + ec.message();
        return false;
    }
    const std::filesystem::path tmp = path.string() + ".tmp";
    {
        std::ofstream stream(tmp, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            error = "could not write " + tmp.string();
            return false;
        }
        stream << json_text;
        stream.flush();
        if (!stream)
        {
            error = "write failed for " + tmp.string();
            return false;
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec)
    {
        error = "could not replace " + path.string() + ": " + ec.message();
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

} // namespace scoreview
} // namespace draxul

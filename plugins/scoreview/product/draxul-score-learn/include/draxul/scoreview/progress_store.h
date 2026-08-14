#pragma once

// Progress-file IO (plans/scoreview-stream.md S0): one JSON file per piece,
// keyed by a stable content hash of the source bytes, written atomically
// (tmp + rename) so a crash can never truncate the player's history.

#include <filesystem>
#include <string>

namespace draxul
{
namespace scoreview
{

// Stable 64-bit FNV-1a of the source bytes — the piece's identity across
// renames and moves.
std::string piece_hash(std::string_view source_bytes);

// <progress_dir>/<hash>.json
std::filesystem::path progress_path(
    const std::filesystem::path& progress_dir, std::string_view source_bytes);

// Empty string when the file doesn't exist (a fresh start, not an error).
std::string load_progress(const std::filesystem::path& path);

// Creates parent directories; writes tmp + rename. False (with `error`)
// on IO failure.
bool save_progress_atomic(
    const std::filesystem::path& path, const std::string& json_text, std::string& error);

} // namespace scoreview
} // namespace draxul

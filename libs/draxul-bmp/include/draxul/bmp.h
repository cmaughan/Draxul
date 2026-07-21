#pragma once

#include <draxul/types.h>
#include <filesystem>
#include <optional>

namespace draxul
{

bool write_bmp_rgba(const std::filesystem::path& path, const CapturedFrame& frame);
std::optional<CapturedFrame> read_bmp_rgba(const std::filesystem::path& path);

} // namespace draxul

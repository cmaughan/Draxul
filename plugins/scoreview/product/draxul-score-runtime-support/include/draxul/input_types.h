#pragma once

#include <cstdint>

namespace draxul
{

using ModifierFlags = uint32_t;
inline constexpr ModifierFlags kModNone = 0;
inline constexpr ModifierFlags kModShift = 0x0003;
inline constexpr ModifierFlags kModCtrl = 0x00c0;
inline constexpr ModifierFlags kModAlt = 0x0300;
inline constexpr ModifierFlags kModSuper = 0x0c00;

} // namespace draxul

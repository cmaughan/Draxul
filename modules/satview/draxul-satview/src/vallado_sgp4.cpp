// Adapter for the CelesTrak/Vallado AIAA-2006-6753 SGP4 reference source.
// The upstream C++ file is compiled through this wrapper so non-MSVC builds
// can provide the one secure-CRT alias still used by the 2020 code.

#ifndef _MSC_VER
#include <cstring>
#define strcpy_s(destination, destination_size, source) std::strcpy((destination), (source))
#endif

#include "SGP4.cpp"

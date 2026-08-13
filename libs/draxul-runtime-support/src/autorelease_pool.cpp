#include <draxul/autorelease_pool.h>

#ifndef __APPLE__

namespace draxul
{

ScopedAutoreleasePool::ScopedAutoreleasePool() = default;
ScopedAutoreleasePool::~ScopedAutoreleasePool() = default;

} // namespace draxul

#endif

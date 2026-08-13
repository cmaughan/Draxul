#include <draxul/autorelease_pool.h>

extern "C" void* objc_autoreleasePoolPush(void);
extern "C" void objc_autoreleasePoolPop(void* context);

namespace draxul
{

ScopedAutoreleasePool::ScopedAutoreleasePool()
    : pool_(objc_autoreleasePoolPush())
{
}

ScopedAutoreleasePool::~ScopedAutoreleasePool()
{
    objc_autoreleasePoolPop(pool_);
}

} // namespace draxul

#pragma once

namespace draxul
{

// Bounds Objective-C autoreleased objects created while one app-pump
// iteration crosses SDL/Cocoa and Metal. It is a no-op off macOS.
class ScopedAutoreleasePool
{
public:
    ScopedAutoreleasePool();
    ~ScopedAutoreleasePool();

    ScopedAutoreleasePool(const ScopedAutoreleasePool&) = delete;
    ScopedAutoreleasePool& operator=(const ScopedAutoreleasePool&) = delete;

private:
    void* pool_ = nullptr;
};

} // namespace draxul

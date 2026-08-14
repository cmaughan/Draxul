#pragma once

#ifdef __OBJC__

// RAII wrapper for Objective-C object pointers.
// Stores a CF-retained void* so the struct is layout-compatible with void* in
// non-ObjC translation units.  Compiled with -fobjc-arc; all bridge casts are
// ARC-aware.
template <typename T>
class ObjCRef
{
public:
    ObjCRef() = default;

    explicit ObjCRef(T obj) noexcept
        : ptr_(obj ? (__bridge_retained void*)obj : nullptr)
    {
    }

    ~ObjCRef()
    {
        if (ptr_)
            (void)(__bridge_transfer id)ptr_;
    }

    ObjCRef(const ObjCRef&) = delete;
    ObjCRef& operator=(const ObjCRef&) = delete;

    ObjCRef(ObjCRef&& other) noexcept
        : ptr_(other.ptr_)
    {
        other.ptr_ = nullptr;
    }

    ObjCRef& operator=(ObjCRef&& other) noexcept
    {
        if (this != &other)
        {
            if (ptr_)
                (void)(__bridge_transfer id)ptr_;
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    // Returns a weak (non-owning) bridge to the underlying object.
    T get() const noexcept
    {
        return (__bridge T)ptr_;
    }

    explicit operator bool() const noexcept
    {
        return ptr_ != nullptr;
    }

    // Releases the current object and optionally retains a new one.
    void reset(T obj = nullptr) noexcept
    {
        if (ptr_)
            (void)(__bridge_transfer id)ptr_;
        ptr_ = obj ? (__bridge_retained void*)obj : nullptr;
    }

    // Transfers CF ownership out of this wrapper into an ARC-managed variable,
    // leaving the wrapper empty.  Equivalent to __bridge_transfer at a call site.
    T take() noexcept
    {
        T result = (__bridge_transfer T)ptr_;
        ptr_ = nullptr;
        return result;
    }

private:
    void* ptr_ = nullptr;
};

#endif // __OBJC__

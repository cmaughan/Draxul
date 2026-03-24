#pragma once

#include <draxul/types.h>
#include <optional>

namespace draxul
{

class ICaptureRenderer
{
public:
    virtual ~ICaptureRenderer() = default;
    virtual void request_frame_capture() = 0;
    virtual std::optional<CapturedFrame> take_captured_frame() = 0;
};

} // namespace draxul

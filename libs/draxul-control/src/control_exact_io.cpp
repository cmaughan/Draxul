#include "control_exact_io.h"

#include <utility>

namespace draxul::control_detail
{

IoAttemptResult IoAttemptResult::progress(size_t transferred)
{
    return { IoAttemptKind::Progress, transferred, {} };
}

IoAttemptResult IoAttemptResult::retry()
{
    return { IoAttemptKind::Retry, 0, {} };
}

IoAttemptResult IoAttemptResult::end_of_stream(TransportError error)
{
    return { IoAttemptKind::EndOfStream, 0, std::move(error) };
}

IoAttemptResult IoAttemptResult::failure(TransportError error)
{
    return { IoAttemptKind::Failure, 0, std::move(error) };
}

namespace
{

TransportStatus invalid_progress(TransportStage stage)
{
    return TransportStatus::failure({
        .stage = stage,
        .domain = NativeDomain::None,
        .native_code = 0,
        .classification = FailureClass::IoError,
        .message = "Control transport reported invalid I/O progress.",
    });
}

template <typename Data, typename Attempt>
TransportStatus transfer_exact(const Attempt& attempt,
    Data* data, size_t size, TransportStage stage)
{
    size_t offset = 0;
    while (offset < size)
    {
        auto result = attempt(data + offset, size - offset, stage);
        switch (result.kind)
        {
        case IoAttemptKind::Progress:
            if (result.transferred == 0
                || result.transferred > size - offset)
            {
                return invalid_progress(stage);
            }
            offset += result.transferred;
            break;
        case IoAttemptKind::Retry:
            break;
        case IoAttemptKind::EndOfStream:
        case IoAttemptKind::Failure:
            result.error.stage = stage;
            return TransportStatus::failure(std::move(result.error));
        }
    }
    return TransportStatus::success();
}

} // namespace

TransportStatus read_exact(const PartialRead& read_some,
    void* data, size_t size, TransportStage stage)
{
    return transfer_exact(read_some, static_cast<char*>(data), size, stage);
}

TransportStatus write_exact(const PartialWrite& write_some,
    const void* data, size_t size, TransportStage stage)
{
    return transfer_exact(write_some, static_cast<const char*>(data), size, stage);
}

} // namespace draxul::control_detail

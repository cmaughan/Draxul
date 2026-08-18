#pragma once

#include "control_transport.h"

#include <cstddef>
#include <functional>

namespace draxul::control_detail
{

enum class IoAttemptKind
{
    Progress,
    Retry,
    EndOfStream,
    Failure,
};

struct IoAttemptResult
{
    IoAttemptKind kind = IoAttemptKind::Failure;
    size_t transferred = 0;
    TransportError error;

    static IoAttemptResult progress(size_t transferred);
    static IoAttemptResult retry();
    static IoAttemptResult end_of_stream(TransportError error);
    static IoAttemptResult failure(TransportError error);
};

using PartialRead = std::function<IoAttemptResult(
    void*, size_t, TransportStage)>;
using PartialWrite = std::function<IoAttemptResult(
    const void*, size_t, TransportStage)>;

TransportStatus read_exact(const PartialRead& read_some,
    void* data, size_t size, TransportStage stage);
TransportStatus write_exact(const PartialWrite& write_some,
    const void* data, size_t size, TransportStage stage);

} // namespace draxul::control_detail

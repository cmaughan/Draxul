#include <draxul/scoreview/verdict_archive.h>

#include <cmath>

namespace draxul
{
namespace scoreview
{

long long VerdictArchive::quantize(double stream_q)
{
    return static_cast<long long>(std::llround(stream_q * 1000.0));
}

void VerdictArchive::record(double stream_q, int pitch, NoteVerdict verdict)
{
    entries_[{ quantize(stream_q), pitch }] = verdict;
}

void VerdictArchive::replay(double window_start_q, double window_end_q,
    const std::function<void(double local_q, int pitch, NoteVerdict verdict)>& apply) const
{
    const double window_end_local = window_end_q - window_start_q;
    for (const auto& [key, verdict] : entries_)
    {
        const double local_q = static_cast<double>(key.first) / 1000.0 - window_start_q;
        if (local_q >= -1e-6 && local_q <= window_end_local)
            apply(local_q, key.second, verdict);
    }
}

} // namespace scoreview
} // namespace draxul

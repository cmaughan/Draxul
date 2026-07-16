#include <draxul/scoreview/stream_program.h>

#include <algorithm>

namespace draxul
{
namespace scoreview
{

void StreamProgram::clear()
{
    program_.clear();
    slot_start_q_.assign(1, 0.0);
}

void StreamProgram::append(StreamBarPlan plan, double quarters)
{
    program_.push_back(std::move(plan));
    slot_start_q_.push_back(slot_start_q_.back() + quarters);
}

double StreamProgram::slot_start_q(int slot) const
{
    const int clamped = std::clamp(slot, 0, static_cast<int>(slot_start_q_.size()) - 1);
    return slot_start_q_[static_cast<size_t>(clamped)];
}

double StreamProgram::slot_quarters(int slot) const
{
    return slot_start_q(slot + 1) - slot_start_q(slot);
}

int StreamProgram::slot_at(double stream_q) const
{
    const int slots = size();
    for (int slot = 0; slot < slots; ++slot)
    {
        if (stream_q < slot_start_q_[static_cast<size_t>(slot) + 1])
            return slot;
    }
    return slots > 0 ? slots - 1 : 0;
}

StreamProgram::SourceRef StreamProgram::source_at(double stream_q) const
{
    SourceRef ref;
    if (program_.empty())
        return ref;
    const int slot = slot_at(stream_q);
    const StreamBarPlan& p = plan(slot);
    ref.source_bar = p.source_bar;
    ref.drill = p.kind == StreamBarPlan::Kind::Drill;
    if (!ref.drill)
        ref.source_q = p.source_start_q + (stream_q - slot_start_q(slot));
    return ref;
}

} // namespace scoreview
} // namespace draxul

#include <draxul/scoreview/stream_program.h>

#include <draxul/scoreview/source_slicer.h>

#include <algorithm>
#include <utility>

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

void StreamProgram::insert(int at_slot, StreamBarPlan plan, double quarters)
{
    const int clamped = std::clamp(at_slot, 0, size());
    program_.insert(program_.begin() + clamped, std::move(plan));
    // Rebuild the prefix sums from the splice point: every later slot moves
    // `quarters` further along the stream axis.
    slot_start_q_.insert(
        slot_start_q_.begin() + clamped + 1, slot_start_q_[static_cast<size_t>(clamped)]);
    for (size_t slot = static_cast<size_t>(clamped) + 1; slot < slot_start_q_.size(); ++slot)
        slot_start_q_[slot] += quarters;
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
    ref.drill = p.kind == StreamBarPlan::Kind::Drill && !p.drill_trains_source;
    if (!ref.drill)
        ref.source_q = p.source_start_q + (stream_q - slot_start_q(slot));
    return ref;
}

namespace
{
StreamBarPlan source_bar_plan(
    const SourceSlicer& slicer, StreamBarPlan::Kind kind, int bar, std::string reason)
{
    StreamBarPlan plan;
    plan.kind = kind;
    plan.source_bar = bar;
    plan.source_start_q = slicer.bar_start_q(bar);
    plan.reason = std::move(reason);
    return plan;
}

StreamBarPlan fabricated_plan(const SourceSlicer& slicer, int reference_bar, std::string xml,
    std::string reason, bool trains_source)
{
    StreamBarPlan plan;
    plan.kind = StreamBarPlan::Kind::Drill;
    plan.source_bar = reference_bar;
    plan.source_start_q = slicer.bar_start_q(reference_bar);
    plan.drill_trains_source = trains_source;
    plan.drill_xml = std::move(xml);
    plan.reason = std::move(reason);
    return plan;
}
} // namespace

void append_source_bar(StreamProgram& program, const SourceSlicer& slicer,
    StreamBarPlan::Kind kind, int bar, std::string reason)
{
    program.append(source_bar_plan(slicer, kind, bar, std::move(reason)), slicer.bar_quarters(bar));
}

void append_fabricated(StreamProgram& program, const SourceSlicer& slicer, int reference_bar,
    std::string xml, std::string reason, bool trains_source)
{
    program.append(
        fabricated_plan(slicer, reference_bar, std::move(xml), std::move(reason), trains_source),
        slicer.bar_quarters(reference_bar));
}

void insert_source_bar(StreamProgram& program, const SourceSlicer& slicer, int at_slot,
    StreamBarPlan::Kind kind, int bar, std::string reason)
{
    program.insert(
        at_slot, source_bar_plan(slicer, kind, bar, std::move(reason)), slicer.bar_quarters(bar));
}

void insert_fabricated(StreamProgram& program, const SourceSlicer& slicer, int at_slot,
    int reference_bar, std::string xml, std::string reason, bool trains_source)
{
    program.insert(at_slot,
        fabricated_plan(slicer, reference_bar, std::move(xml), std::move(reason), trains_source),
        slicer.bar_quarters(reference_bar));
}

} // namespace scoreview
} // namespace draxul

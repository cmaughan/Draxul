#pragma once

// Splice-aware slot cooldowns (kanban 22): the "don't serve this again for N
// slots" gate every rotating special shares, plus the shift that keeps those
// gates consistent when plan_urgent splices a correction into the program.
// Composer-agnostic — a second composer reuses the type and inherits the
// splice-shift contract as a mechanism instead of hand-writing it.

#include <map>

namespace draxul
{
namespace scoreview
{

// Type-erased shift so a composer can keep cooldowns with different key types
// in one list and shift them all in a single loop after a splice — a new
// cooldown registered in that list cannot be forgotten by the shift.
class ISlotShift
{
public:
    virtual ~ISlotShift() = default;
    // Every recorded slot at or past `at_slot` moves one slot later, matching
    // StreamProgram::insert shifting the program's geometry by one slot.
    virtual void shift_from(int at_slot) = 0;
};

template <typename Key>
class SlotCooldowns final : public ISlotShift
{
public:
    explicit SlotCooldowns(int cooldown_slots)
        : cooldown_slots_(cooldown_slots)
    {
    }

    // True when `key` was never served, or its last service is at least
    // `cooldown_slots` slots behind `slot`.
    bool ready(const Key& key, int slot) const
    {
        const auto last = last_.find(key);
        return last == last_.end() || slot - last->second >= cooldown_slots_;
    }
    // Record that `key` was served at `slot`.
    void note(const Key& key, int slot)
    {
        last_[key] = slot;
    }
    void clear()
    {
        last_.clear();
    }
    void shift_from(int at_slot) override
    {
        for (auto& [key, slot] : last_)
        {
            (void)key;
            if (slot >= at_slot)
                ++slot;
        }
    }

private:
    std::map<Key, int> last_;
    int cooldown_slots_;
};

} // namespace scoreview
} // namespace draxul

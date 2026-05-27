#include "instrument/voice_pool.hpp"

#include <limits>

void VoicePool::kill_all()
{
    for (auto& v : voices_) v.kill();
}

Voice* VoiceAllocator::acquire(VoicePool& pool, bool voice_stealing, int preferred_slot)
{
    // GUI voice-select override: caller asked for a specific slot. Honour it
    // unconditionally — if it's already playing, this re-triggers (steals
    // itself).
    if (preferred_slot >= 0 && static_cast<size_t>(preferred_slot) < pool.size())
        return &pool[static_cast<size_t>(preferred_slot)];

    // First inactive slot wins.
    for (auto& v : pool)
        if (!v.is_active()) return &v;

    // All busy: optionally steal the oldest.
    if (!voice_stealing) return nullptr;

    Voice* victim = nullptr;
    uint64_t lo = std::numeric_limits<uint64_t>::max();
    for (auto& v : pool)
    {
        if (v.launch_seq() < lo)
        {
            lo     = v.launch_seq();
            victim = &v;
        }
    }
    return victim;
}

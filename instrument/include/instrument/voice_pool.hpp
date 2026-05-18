#ifndef INSTRUMENT_VOICE_POOL_HPP
#define INSTRUMENT_VOICE_POOL_HPP

#include "instrument/constants.hpp"
#include "instrument/voice.hpp"

#include <array>
#include <cstddef>

/** Fixed-size voice storage. Iteration is in declaration order. */
class VoicePool
{
public:
    using Container = std::array<Voice, max_voices>;
    using iterator       = Container::iterator;
    using const_iterator = Container::const_iterator;

    iterator       begin()       { return voices_.begin(); }
    iterator       end()         { return voices_.end(); }
    const_iterator begin() const { return voices_.begin(); }
    const_iterator end()   const { return voices_.end(); }

    Voice&       operator[](size_t i)       { return voices_[i]; }
    const Voice& operator[](size_t i) const { return voices_[i]; }

    constexpr size_t size() const { return voices_.size(); }

    /** Kill every active voice. */
    void kill_all();

    /** Kill the oldest active voice (smallest launch_seq). No-op if none active. */
    void kill_oldest();

private:
    Container voices_{};
};

/** Voice allocation policy.
 *
 * acquire():
 *   - first inactive slot wins; else
 *   - if voice_stealing is true → return the slot with the smallest
 *     launch_seq (the oldest active voice); else
 *   - return nullptr (caller silently drops the launch).
 */
class VoiceAllocator
{
public:
    Voice* acquire(VoicePool& pool, bool voice_stealing);
};

#endif

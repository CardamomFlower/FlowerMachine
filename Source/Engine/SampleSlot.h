#pragma once

#include <JuceHeader.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <utility>
#include <vector>

#include "SampleData.h"

namespace flowermachine
{
//==============================================================================
/** Lock-free, real-time-safe handoff of a std::shared_ptr<const SampleData>
    from the message thread (loader) to the audio thread (voices).

    Copied from Svarog (../CardamomTools/Svarog/Source/Generators/Sample/SampleSlot.h)
    with the payload type changed - CLAUDE.md real-time rule 5.

    A monotonic ring of N slots + an atomic live index, plus a message-thread
    retire list.

    - acquire() (audio thread): atomic load of the live index + a shared_ptr copy
      (a wait-free refcount increment). The voice holds the returned pointer for
      the whole hit (RAII lifetime). Releasing it later only decrements and is
      never the last reference (a slot or the retire list still holds one), so the
      deleter never runs on the audio thread.
    - publish() / prune() (message thread): write the next ring slot, flip the
      index, and free old data once no voice references it (use_count() == 1).

    Race-freedom of the shared_ptr OBJECT (its non-atomic pointer members) relies
    on the message thread never writing the slot the audio thread is mid-copy of.
    publish() always writes the slot AFTER the live one and advances monotonically,
    so a slot is only reused after `numSlots` publishes; for the race to occur the
    audio thread would have to be preempted, between the index load and the copy,
    across `numSlots` sample loads (hundreds of ms / seconds) - i.e. only when the
    audio is already catastrophically stalled. Assumes shared_ptr refcount ops are
    atomic & wait-free on the target runtimes (MSVC / libstdc++, x86/ARM).

    DIFFERENCE FROM SVAROG'S ORIGINAL: unload(). Svarog never unloads a slot, so
    publish() alone was enough there; FlowerMachine unloads every hidden page
    (ARCHITECTURE.md section 5, decision D5) and publish(nullptr) frees nothing -
    it writes the NEXT slot, leaving the previously live one still owning the
    decoded audio until the ring wraps 16 publishes later. unload() releases the
    whole ring instead, deferred by RELEASE_DELAY_MS so the slots are only cleared
    long after they stopped being reachable through liveIndex.
*/
class SampleSlot
{
public:
    /** Audio thread: current sample (may be null). Wait-free. */
    SamplePtr acquire() const noexcept
    {
        return slots[(size_t) liveIndex.load (std::memory_order_acquire)];
    }

    /** Message thread: publish new data. */
    void publish (SamplePtr newData)
    {
        const int writeIndex = nextIndex;   // always != current live index

        retired.push_back (slots[(size_t) writeIndex]);   // keep whatever was there alive
        slots[(size_t) writeIndex] = std::move (newData);
        liveIndex.store (writeIndex, std::memory_order_release);
        nextIndex = (writeIndex + 1) % numSlots;

        prune();
    }

    /** Message thread: unloads the cart and gives its memory back.

        acquire() stops handing the sample out at once (the live index moves to a null
        slot); the rest of the ring is released by a later prune(), at least
        RELEASE_DELAY_MS afterwards. That delay is what keeps the release race-free: a
        slot is only written once no acquire() can still be mid-copy of it, and the
        audio thread would have to be stalled for half a second to be caught out.
    */
    void unload()
    {
        publish (nullptr);
        releaseAt = juce::Time::getMillisecondCounter() + RELEASE_DELAY_MS;
        releasePending = true;
    }

    /** Message thread: free retired samples no voice references anymore, and complete
        any unload whose delay has elapsed. Called on the controller's 1 s timer. */
    void prune()
    {
        if (releasePending && juce::Time::getMillisecondCounter() >= releaseAt)
        {
            releasePending = false;

            // Every slot but the live one: the live slot is either the null published by
            // unload(), or a sample published since, which must stay.
            const int live = liveIndex.load (std::memory_order_relaxed);   // message thread is the only writer

            for (int i = 0; i < numSlots; ++i)
                if (i != live && slots[(size_t) i] != nullptr)
                    retired.push_back (std::exchange (slots[(size_t) i], nullptr));
        }

        retired.erase (std::remove_if (retired.begin(), retired.end(),
                                       [] (const SamplePtr& p)
                                       { return p == nullptr || p.use_count() <= 1; }),
                       retired.end());
    }

private:
    static constexpr int numSlots = 16;
    static constexpr juce::uint32 RELEASE_DELAY_MS = 500;

    SamplePtr slots[numSlots];
    std::atomic<int> liveIndex { 0 };
    int nextIndex = 1;                  // message thread only
    std::vector<SamplePtr> retired;     // message thread only
    bool releasePending = false;        // message thread only
    juce::uint32 releaseAt = 0;         // message thread only

    JUCE_LEAK_DETECTOR (SampleSlot)
};

} // namespace flowermachine

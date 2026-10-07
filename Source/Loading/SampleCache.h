#pragma once

#include <JuceHeader.h>

#include <vector>

#include "../Engine/SampleData.h"

namespace flowermachine
{
    /*  Keeps the decoded audio of long files after their page has been hidden, so that showing
        the page again does not decode and resample them a second time. Short files are not
        kept: loading takes time in proportion to length, so theirs is short, and memory is not
        free on a machine doing other work too.

        Bounded by a byte budget, oldest out first. An entry is taken out again when its page
        is shown, so what is held here is only ever audio that nothing on screen is using.

        Holding a sample here can never move its destructor onto the audio thread. What a slot
        holds is never the sample itself but a wrapper made for that one publish (see
        CartEngine::publish); a voice copies the wrapper, and the slot's retire list frees it only
        once nothing else holds it. So every reference to the sample - the wrapper's, this
        cache's - is let go on the message thread.

        Message thread only.
    */
    class SampleCache
    {
    public:
        /** Long enough that loading it again would be noticed (HIDDEN_CACHE_MIN_SECONDS). */
        static bool isWorthKeeping (const SampleData&);

        /** Keeps `sample` as the audio of `file`, dropping the oldest entries to stay within the
            budget. A sample larger than the whole budget is not kept at all. */
        void store (const juce::File& file, SamplePtr sample);

        /** Takes the audio of `file` at `sampleRate` back out, or returns null. A file that has
            changed on disk since it was decoded is a miss, and its stale entry is dropped. */
        SamplePtr take (const juce::File& file, double sampleRate);

        /** Drops everything decoded at any other rate: after a device change it cannot be used. */
        void retainRate (double sampleRate);

        void clear();

    private:
        struct Entry
        {
            juce::String path;
            SamplePtr sample;
            size_t bytes = 0;
        };

        static size_t sizeOf (const SampleData&);
        void remove (size_t index);

        std::vector<Entry> entries;   // oldest first
        size_t bytesHeld = 0;
    };
}

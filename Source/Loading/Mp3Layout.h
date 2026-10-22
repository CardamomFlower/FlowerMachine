#pragma once

#include <JuceHeader.h>

#include <optional>

namespace flowermachine
{
    /*  Which samples of an MP3, as JUCE's MP3 reader decodes it, are the recording.

        An MP3 encoder adds silence at both ends: a delay before the first sample and padding
        after the last, to fill whole frames. The decoder adds a delay of its own. Encoders in
        the LAME family write both amounts into the Info frame at the start of the file, and
        a player that honours them gives back exactly the samples that went in - which is what
        lets an MP3 loop with no gap. JUCE's reader ignores them, so this reads them itself.

        It also counts the frames, because the reader's own length can be wrong: it multiplies
        every stream by 1152 samples a frame, where MPEG-2 and 2.5 Layer III frames carry 576
        and Layer I frames 384, and without an Info frame it guesses the count from the file
        size, which cuts some files short and makes others fail to load at all. Layer I and II
        streams - an MP2 saved with an .mp3 name - are counted the same way and kept whole.
    */
    struct Mp3Layout
    {
        juce::int64 firstSample = 0;   // samples of the decoded stream to drop at the start
        juce::int64 numSamples = 0;    // samples to keep after them

        /** Measures `file`. Nothing when it is not an MPEG audio stream this can measure (a
            free-format one gives no frame sizes), in which case the reader's own length is the
            best there is. */
        static std::optional<Mp3Layout> read (const juce::File& file);
    };
}

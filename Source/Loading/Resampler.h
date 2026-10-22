#pragma once

#include <JuceHeader.h>

#include <functional>
#include <vector>

namespace flowermachine
{
    /*  Converts a whole file to the device's sample rate, once, when it is loaded.

        A polyphase windowed-sinc filter: flat to 20 kHz, images and aliases more than 100 dB
        down, the coefficients for each fractional position read from a table and interpolated
        between neighbouring entries, and the inner products done four lanes at a time. That is
        some thirty times faster than evaluating the sinc tap by tap, which made a long file
        take as long to resample as a song takes to listen to.

        Two properties matter for a cart:
        - The converted file is exactly as long as its converted duration rounds to, and the
          last output sample hands over to the first exactly as the last input sample hands over
          to the first input sample. Output n sits at input time n * inFrames / outFrames, not
          at n * inRate / outRate - the difference is a pitch change of well under a part per
          million, and it is what makes a looped file come round with no jump.
        - Beyond the ends of the file the filter reads the other end, as if the file were
          already looping. A file played as a loop then has no seam; a one-shot only gains a
          trace of its own other end under the start and stop ramps, where it cannot be heard.

        The file is pulled through in chunks, so nothing larger than the output is held.
    */
    class Resampler
    {
    public:
        Resampler (double sourceRate, double targetRate);

        /** Frames the converted file has: inFrames * targetRate / sourceRate, to the nearest
            whole frame, worked out in integers so a length that is exact stays exact. */
        juce::int64 outputLength (juce::int64 inFrames) const noexcept;

        /** Fills `destination` from `startFrame` with the next `numFrames` frames of the source,
            in order from the first. False when the source cannot deliver them. */
        using Source = std::function<bool (juce::AudioBuffer<float>& destination, int startFrame, int numFrames)>;

        /** Converts the `inFrames` frames `source` delivers into all of `output`, which must
            already be outputLength (inFrames) frames long and have the source's channel count
            (one or two). Returns false if the source failed or `cancelled` said stop. */
        bool process (const Source& source, juce::int64 inFrames, juce::AudioBuffer<float>& output,
                      const std::function<bool()>& cancelled) const;

    private:
        void computeRange (const float* const* window, juce::int64 windowStart, int numChannels,
                           juce::int64 inFrames, juce::int64 outFrames,
                           juce::AudioBuffer<float>& output, juce::int64 fromOutput, juce::int64 toOutput) const;

        juce::int64 sourceRateHz = 0, targetRateHz = 0;
        int taps = 0;                // a multiple of 8
        int centre = 0;              // tap that sits on the input sample at or before the output time
        std::vector<float> table;    // one row of `taps` coefficients per stored position, plus one
    };
}

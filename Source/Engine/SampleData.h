#pragma once

#include <JuceHeader.h>

#include <memory>

namespace flowermachine
{
    /*  One decoded cart file, immutable once built (ARCHITECTURE.md section 4):
        1 or 2 channels of float32 already at the device sample rate, so the audio
        thread only copies and scales. Built by SampleLoader on a pool thread,
        handed over through SampleSlot, freed on the message thread only.
    */
    struct SampleData
    {
        juce::AudioBuffer<float> audio;
        double sampleRate = 0.0;            // the rate `audio` is at
        juce::int64 lengthFrames = 0;

        // metadata for the UI
        juce::String sourcePath;
        double sourceSampleRate = 0.0;
        int sourceChannels = 0;
        double durationSeconds = 0.0;
    };

    using SamplePtr = std::shared_ptr<const SampleData>;
}

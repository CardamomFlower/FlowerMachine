#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>

#include "Tune.h"

namespace flowerinstall
{
    /*  A four-channel chiptune, synthesised. No audio file anywhere in this installer.

        The house real-time rules apply here exactly as they do in FlowerMachine's own
        engine: prepareToPlay does every division, pow and coefficient on the message
        thread, and the callback is integer counters and float arithmetic over a const
        tune. No allocation, no locks, no IO, ScopedNoDenormals at the top.

        The pulses are PolyBLEP-corrected and the mix runs through a gentle low-pass and
        a DC-killing high-pass, because a raw square wave at 44.1 kHz is all aliasing
        and no fun to sit next to.
    */
    class ChipSynth : public juce::AudioSource
    {
    public:
        ChipSynth() = default;

        void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
        void releaseResources() override;
        void getNextAudioBlock (const juce::AudioSourceChannelInfo&) override;

        /** Message thread: start the fade-out. */
        void fadeOut() noexcept { fading.store (true); }

        /** Message thread: true once the fade has finished and it is safe to stop the device. */
        bool hasFadedOut() const noexcept { return silent.load(); }

        /** Message thread: the row the sequencer is on, for anything on screen that wants to move in time. */
        int getRowCounter() const noexcept { return rowCounter.load(); }

    private:
        enum class Wave { pulse50, pulse25, triangle, noise };

        struct Voice
        {
            Wave wave = Wave::pulse50;
            float phase = 0.0f;
            float increment = 0.0f;
            float level = 0.0f;      // envelope
            float target = 0.0f;
            float attack = 0.0f;
            float decay = 0.0f;
            float sustain = 0.0f;
            float amplitude = 0.0f;
            juce::int8 baseNote = 0;
            juce::uint8 arp = 0;
            juce::uint32 noiseState = 0x1234u;
            bool gated = false;
        };

        void startNote (Voice&, juce::int8 note, juce::uint8 arp) noexcept;
        void advanceRow() noexcept;
        float renderVoice (Voice&, int arpStep) noexcept;

        std::array<Voice, tune::NUM_CHANNELS> voices;
        std::array<float, 128> noteIncrement {};   // MIDI note -> phase increment, filled in prepareToPlay

        double currentSampleRate = 44100.0;
        int samplesPerRow = 4410;
        int samplesToNextRow = 0;
        int orderIndex = 0;
        int rowIndex = 0;
        int arpCounter = 0;
        int samplesPerArpStep = 400;
        int samplesToNextArpStep = 0;
        int arpStep = 0;

        float masterGain = 0.0f;
        float gainStep = 0.0f;
        float fadeStep = 0.0f;

        // one-pole filters, coefficients computed in prepareToPlay
        float lowPassCoeff = 0.0f, lowPassStateL = 0.0f;
        float highPassCoeff = 0.0f, highPassStateL = 0.0f, highPassPrevL = 0.0f;

        std::atomic<bool> fading { false };
        std::atomic<bool> silent { false };
        std::atomic<int> rowCounter { 0 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChipSynth)
    };
}

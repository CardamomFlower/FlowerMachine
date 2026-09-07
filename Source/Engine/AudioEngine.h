#pragma once

#include <JuceHeader.h>

#include <atomic>
#include <functional>

#include "CartEngine.h"

namespace flowermachine
{
    /*  Owns the output device and the audio callback (ARCHITECTURE.md section 4):
        the device manager initialised from the saved state, an AudioSourcePlayer,
        the CartEngine and the Settings test tone, mixed in one block.

        All public methods are message-thread only. The callback side follows the
        house real-time rules: no allocation, no locks, no IO.
    */
    class AudioEngine : private juce::ChangeListener
    {
    public:
        explicit AudioEngine (juce::PropertiesFile& settingsToUse);
        ~AudioEngine() override;

        CartEngine& getCartEngine() noexcept             { return cartEngine; }
        const CartEngine& getCartEngine() const noexcept { return cartEngine; }

        /** Plays the test tone burst (TEST_TONE_* in Constants.h). */
        void triggerTestTone();

        /** Silences every cart and the tone, through the DECLICK_MS ramp. */
        void stopAll();

        juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }

        /** False while no device is open: nothing drains the command FIFO then, so the
            controller must not queue play commands that would all fire at once later. */
        bool hasOutputDevice() const noexcept { return deviceManager.getCurrentAudioDevice() != nullptr; }

        /** The rate the callback runs at, 0 while no device is open. */
        double getSampleRate() const noexcept { return cartEngine.getSampleRate(); }

        /** "WASAPI - Speakers (Realtek) - 48.0 kHz - 480 samples", or why there is no output. */
        juce::String getDeviceDescription() const;

        std::function<void()> onDeviceChanged;   // called on the message thread

    private:
        struct TestTone
        {
            void prepare (double newSampleRate);
            void render (juce::AudioBuffer<float>& output, int startSample, int numSamples);   // adds

            // message thread -> audio thread
            std::atomic<bool> triggerRequested { false };
            std::atomic<bool> stopRequested { false };

            // audio-thread-only state
            double sampleRate = 44100.0;
            double phase = 0.0;
            juce::int64 remaining = 0;     // frames left in the current burst
            float envelope = 0.0f;         // linear declick ramp, 0..1
            float envelopeStep = 0.0f;
            bool active = false;
            bool releasing = false;
        };

        struct Source : public juce::AudioSource
        {
            explicit Source (AudioEngine& ownerToUse) : owner (ownerToUse) {}

            void prepareToPlay (int samplesPerBlockExpected, double newSampleRate) override;
            /*  The device has stopped: no callback will run again until prepareToPlay, so
                anything still flagged as sounding would stay flagged for ever - the grid would
                show playing pads and a sequence would wait for a step that can never end.
            */
            void releaseResources() override { owner.cartEngine.reset(); }
            void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override;

            AudioEngine& owner;
        };

        void changeListenerCallback (juce::ChangeBroadcaster*) override;

        juce::PropertiesFile& settings;
        juce::AudioDeviceManager deviceManager;
        juce::AudioSourcePlayer player;
        CartEngine cartEngine;
        TestTone tone;
        Source source { *this };
        juce::String initError;   // what initialise() said when no device could be opened

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
    };
}

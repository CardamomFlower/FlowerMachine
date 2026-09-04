#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>

#include "../Constants.h"
#include "Commands.h"
#include "SampleData.h"
#include "SampleSlot.h"

namespace flowermachine
{
    /*  The cart mixer (ARCHITECTURE.md section 4).

        Message-thread API on top, audio-thread render() below; the two meet only
        through the command FIFO, the SampleSlots and the per-cart atomics. render()
        and everything it calls follow the house real-time rules: no allocation, no
        locks, no IO, bounded loops.

        Voices: a fixed pool of MAX_VOICES, each tagged with a cart id. play() on a
        cart that is already sounding releases the old voice over DECLICK_MS and
        starts a new one from frame 0 (restart, decision D1). play() also releases
        every other playing cart that is not looping (D2 revised): a jingle replaces
        the one before it, a looping bed keeps going until its Stop or STOP ALL.
        A non-looping voice fades over the last DECLICK_MS of its file; a looping
        one wraps seamlessly.
    */
    class CartEngine
    {
    public:
        CartEngine() = default;

        //==============================================================================
        // Message thread
        void publish (int cartId, SamplePtr sample);
        void unload (int cartId);                           // gives the cart's memory back (SampleSlot::unload)
        void pruneAll();                                    // frees retired samples no voice plays any more
        bool push (const Command&);                         // false when the FIFO is full
        void setGainDb (int cartId, float gainDb);
        void setLoop (int cartId, bool shouldLoop);

        //==============================================================================
        // Read by the UI at UI_REFRESH_HZ (lock-free)
        bool isPlaying (int cartId) const noexcept;
        juce::int64 getPlayheadFrames (int cartId) const noexcept;
        juce::int64 getLengthFrames (int cartId) const noexcept;
        double getSampleRate() const noexcept { return sampleRate.load(); }   // 0 until a device runs

        //==============================================================================
        // Audio thread
        void prepare (double newSampleRate, int maxBlockSize);
        void render (juce::AudioBuffer<float>& output, int startSample, int numSamples);   // adds into `output`

    private:
        struct CartRt
        {
            SampleSlot slot;
            std::atomic<float> gain { 1.0f };
            std::atomic<bool> loop { false };
            std::atomic<bool> playing { false };
            std::atomic<juce::int64> playhead { 0 };
            std::atomic<juce::int64> length { 0 };
        };

        struct Voice
        {
            enum class Phase { idle, attack, sustain, release };

            SamplePtr sample;
            int cartId = -1;
            juce::int64 position = 0;
            Phase phase = Phase::idle;
            float envelope = 0.0f;
            float releaseStep = 0.0f;   // per voice: the tail at the end of a file may be shorter than DECLICK_MS
            juce::SmoothedValue<float> gain;

            bool isActive() const noexcept { return phase != Phase::idle; }
        };

        void handle (const Command&);
        void startVoice (int cartId);
        void beginRelease (Voice&, float step);
        void releaseVoicesOf (int cartId);
        void releaseOthersNotLooping (int cartId);
        void releaseAll();
        void retire (Voice&);
        void renderVoice (Voice&, juce::AudioBuffer<float>& output, int startSample, int numSamples);

        static bool isValidCart (int cartId) noexcept { return juce::isPositiveAndBelow (cartId, MAX_CARTS); }

        std::array<CartRt, MAX_CARTS> carts;
        std::array<Voice, MAX_VOICES> voices;
        std::array<int, MAX_CARTS> voiceCount {};    // audio thread only
        CommandFifo commands;

        std::atomic<double> sampleRate { 0.0 };
        float envelopeStep = 1.0f;                    // audio thread only
        juce::int64 releaseFrames = 0;                // audio thread only

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CartEngine)
    };
}

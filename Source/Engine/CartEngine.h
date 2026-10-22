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
    /*  The cart mixer.

        Message-thread API on top, audio-thread render() below; the two meet only
        through the command FIFO, the SampleSlots and the per-cart atomics. render()
        and everything it calls allocate nothing, take no lock, do no IO, and loop a
        bounded number of times.

        Voices: a fixed pool of MAX_VOICES, each tagged with a cart id. play() on a
        cart that is already sounding releases the old voice over DECLICK_MS and
        starts a new one from frame 0, so clicking a sounding cart restarts it. play()
        also releases every other playing cart that is not looping: a jingle replaces
        the one before it, a looping bed keeps going until its Stop or STOP ALL.
        A non-looping voice fades over the last DECLICK_MS of its file; a looping
        one wraps seamlessly.

        Moving a pad or a page renumbers carts (swapCarts, movePage); voices follow their
        cart to its new id, so nothing stops because the layout changed.
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

        /** Monotonic count of voice starts on this cart; see CartRt::startCount. */
        juce::uint32 getStartCount (int cartId) const noexcept;
        juce::int64 getPlayheadFrames (int cartId) const noexcept;
        juce::int64 getLengthFrames (int cartId) const noexcept;
        double getSampleRate() const noexcept { return sampleRate.load(); }   // 0 until a device runs

        /** How many swapCarts / movePage commands the audio thread has applied so far. */
        juce::uint32 getLayoutChangesApplied() const noexcept { return layoutChangesApplied.load(); }

        //==============================================================================
        // Audio thread
        void prepare (double newSampleRate, int maxBlockSize);

        /** Silences everything and drops every pending command except a change of layout,
            which it applies: once a move is queued, each cart's gain and loop flag reach their
            new id only through it. Called when the device starts and again when it stops: a
            stopped device runs no callback, so without this a cart that was sounding would
            stay flagged as playing for ever. */
        void reset();
        void render (juce::AudioBuffer<float>& output, int startSample, int numSamples);   // adds into `output`

    private:
        struct CartRt
        {
            SampleSlot slot;
            std::atomic<float> gain { 1.0f };
            std::atomic<bool> loop { false };
            std::atomic<bool> playing { false };

            /*  Raised by one every time a voice starts on this cart. `playing` is a level and
                the UI polls it 30 times a second, so a cart shorter than one poll can go up
                and down unseen; this counter cannot be missed. A sequence uses it to tell
                "the step has been and gone" from "the step never started".
            */
            std::atomic<juce::uint32> startCount { 0 };
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
            bool ignoreLoop = false;    // set for a voice started as part of a sequence
            juce::SmoothedValue<float> gain;

            bool isActive() const noexcept { return phase != Phase::idle; }
        };

        void handle (const Command&);
        void startVoice (int cartId, bool ignoreLoop);
        void beginRelease (Voice&, float step);
        void releaseVoicesOf (int cartId);
        void releaseOthersNotLooping (int cartId);
        void releaseAll();

        /** swapCarts / movePage: every voice, and every cart's gain and loop flag, take the
            cart's new id, so a pad that was moved while it sounded keeps playing as it was,
            shows as playing where it now is, and its Stop still reaches it. The slots are the
            message thread's business: it publishes or unloads the carts concerned itself. */
        void followLayoutChange (const Command&);
        void retire (Voice&);
        void renderVoice (Voice&, juce::AudioBuffer<float>& output, int startSample, int numSamples);

        static bool isValidCart (int cartId) noexcept { return juce::isPositiveAndBelow (cartId, MAX_CARTS); }

        std::array<CartRt, MAX_CARTS> carts;
        std::array<Voice, MAX_VOICES> voices;
        std::array<int, MAX_CARTS> voiceCount {};    // audio thread only
        std::array<float, MAX_CARTS> movedGain {};   // audio thread only: scratch for followLayoutChange
        std::array<bool, MAX_CARTS> movedLoop {};    // audio thread only: scratch for followLayoutChange
        CommandFifo commands;

        std::atomic<double> sampleRate { 0.0 };
        std::atomic<juce::uint32> layoutChangesApplied { 0 };
        float envelopeStep = 1.0f;                    // audio thread only
        juce::int64 releaseFrames = 0;                // audio thread only

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CartEngine)
    };
}

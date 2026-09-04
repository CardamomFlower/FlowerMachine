#pragma once

#include <JuceHeader.h>

namespace flowermachine
{
    /*  empty     no cart here
        unloaded  assigned, but not resident (its page is hidden — section 5)
        loading   decoding on the pool
        ready     resident, triggerable
        missing   the file resolved nowhere
        error     the file could not be decoded
    */
    enum class CartState { empty, unloaded, loading, ready, missing, error };

    /*  Runtime state of one cart as the UI sees it (ARCHITECTURE.md sections 5-6).
        Message thread only, kept in step with the preset by the Controller; the
        audio-side facts (playing, playhead) live in CartEngine's atomics.
    */
    struct CartStatus
    {
        CartState state = CartState::empty;
        juce::File file;
        juce::String title;
        juce::Colour colour;            // transparent = none
        juce::String error;             // what went wrong, for missing / error
        float gainDb = 0.0f;
        bool loop = false;
        double durationSeconds = 0.0;   // valid once ready
        double sampleRate = 0.0;        // rate the ready data was decoded at

        bool isAssigned() const noexcept { return state != CartState::empty; }
    };
}

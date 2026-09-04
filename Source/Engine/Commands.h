#pragma once

#include <JuceHeader.h>

#include <array>

#include "../Constants.h"

namespace flowermachine
{
    /*  Message thread -> audio thread (ARCHITECTURE.md section 4). Gain and loop
        travel as per-cart atomics instead, so these are the only commands.
    */
    struct Command
    {
        enum class Type : juce::uint8 { play, stop, stopAll, flushAll };

        Type type = Type::stopAll;
        juce::uint16 cartId = 0;
    };

    /*  Single producer (message thread), single consumer (audio thread), fixed size. */
    class CommandFifo
    {
    public:
        /** Message thread. Returns false when the queue is full — surfaced by the caller, never hidden. */
        bool push (const Command& command) noexcept
        {
            const auto scope = fifo.write (1);

            if (scope.blockSize1 < 1)
                return false;

            buffer[(size_t) scope.startIndex1] = command;
            return true;
        }

        /** Audio thread. */
        bool pop (Command& out) noexcept
        {
            const auto scope = fifo.read (1);

            if (scope.blockSize1 < 1)
                return false;

            out = buffer[(size_t) scope.startIndex1];
            return true;
        }

    private:
        juce::AbstractFifo fifo { COMMAND_FIFO_SIZE };
        std::array<Command, COMMAND_FIFO_SIZE> buffer {};
    };
}

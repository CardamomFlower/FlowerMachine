#pragma once

#include <JuceHeader.h>

#include <array>

#include "../Constants.h"

namespace flowermachine
{
    /*  Message thread -> audio thread. Gain and loop
        travel as per-cart atomics instead, so these are the only commands.

        swapCarts and movePage follow a change of layout: a cart id is its page and cell, so
        moving a pad or a page gives carts new ids, and whatever is sounding has to be told
        where its cart went or Stop would no longer reach it.
    */
    struct Command
    {
        enum class Type : juce::uint8 { play, stop, stopAll, flushAll, swapCarts, movePage };

        Type type = Type::stopAll;
        juce::uint16 cartId = 0;

        /** play only: this voice ignores the cart's Loop flag and ends at the end of the file.
            A sequence needs it - a looping pad would otherwise hold the queue for ever. */
        bool ignoreLoop = false;

        /** swapCarts: the cart that trades places with cartId.
            movePage: cartId is the index of the page that moves and this is where it goes. */
        juce::uint16 target = 0;
    };

    /** The index a page ends up at when the page at `from` is moved to `to`: the pages in
        between close the gap it leaves, as when a tab is dragged along a strip. */
    inline int pageAfterMove (int page, int from, int to) noexcept
    {
        if (page == from)
            return to;

        if (from < to && page > from && page <= to)
            return page - 1;

        if (to < from && page >= to && page < from)
            return page + 1;

        return page;
    }

    /** The id a cart has after a swapCarts or movePage command, and its own id for any other
        command. The engine and the controller both apply this, so they cannot disagree. */
    inline int cartIdAfter (const Command& command, int cartId) noexcept
    {
        if (command.type == Command::Type::swapCarts)
            return cartId == command.cartId ? command.target
                 : cartId == command.target ? command.cartId
                                            : cartId;

        if (command.type == Command::Type::movePage)
            return pageAfterMove (cartId / CARTS_PER_PAGE, command.cartId, command.target) * CARTS_PER_PAGE
                     + cartId % CARTS_PER_PAGE;

        return cartId;
    }

    /*  Single producer (message thread), single consumer (audio thread), fixed size. */
    class CommandFifo
    {
    public:
        /** Message thread. Returns false when the queue is full - surfaced by the caller, never hidden. */
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

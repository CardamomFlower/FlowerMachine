#pragma once

#include <JuceHeader.h>

#include <memory>

#include "../Control/Controller.h"
#include "../Engine/CartEngine.h"
#include "../Model/CartStatus.h"

namespace flowermachine
{
    /*  One cart (ARCHITECTURE.md section 6), drawn after the design canvas: a play
        area (colour band, title, time, progress) plus three child controls —
        STOP, LOOP, Gain. The children take their own clicks; only the play area
        triggers. Right-click opens the cart menu.
    */
    /*  The strip controls sit over the bottom third of every ready cart, and JUCE acts on a
        right-click like any other: a right-press on LOOP would toggle the loop flag and edit
        the preset, a right-drag on the knob would change the gain. Section 6 gives the whole
        cart to the context menu, so these two hand a popup-menu press back to the cart and
        never let the base class see it.
    */
    template <typename Base>
    class PopupAwareControl : public Base
    {
    public:
        using Base::Base;

        std::function<void()> onPopupMenu;

        void mouseDown (const juce::MouseEvent& e) override
        {
            popupPressed = e.mods.isPopupMenu();

            if (popupPressed)
            {
                if (onPopupMenu != nullptr)
                    onPopupMenu();

                return;
            }

            Base::mouseDown (e);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! popupPressed)
                Base::mouseDrag (e);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (popupPressed)
            {
                popupPressed = false;
                return;
            }

            Base::mouseUp (e);
        }

    private:
        bool popupPressed = false;
    };

    class CartButton : public juce::Component,
                       public juce::SettableTooltipClient
    {
    public:
        CartButton (int cellIndex, Controller&, const CartEngine&);

        /** page * CARTS_PER_PAGE + cell; set by the grid when the visible page changes. */
        void setCartId (int newCartId);

        /** Polls status and engine atomics; repaints only when something visible changed. */
        void refresh();

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

    private:
        void showMenu();
        void chooseFile (bool relocate);
        void renameCart();
        void chooseColour();

        static juce::String formatTime (double seconds);   // m:ss
        static const juce::String& audioWildcard();
        int stripHeight() const;
        juce::Rectangle<int> playArea() const;

        Controller& controller;
        const CartEngine& engine;
        const int cell;
        int cartId;

        PopupAwareControl<juce::TextButton> stopButton { "STOP" };
        PopupAwareControl<juce::TextButton> loopButton { "LOOP" };
        PopupAwareControl<juce::Slider> gainSlider;
        std::unique_ptr<juce::FileChooser> chooser;

        // cached view state, compared on refresh()
        CartState state = CartState::empty;
        juce::String title;
        juce::String timeText;
        juce::Colour colour;
        bool playing = false;
        float progress = 0.0f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CartButton)
    };
}

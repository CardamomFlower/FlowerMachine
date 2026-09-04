#pragma once

#include <JuceHeader.h>

#include "../Control/Controller.h"
#include "../Engine/CartEngine.h"
#include "CartButton.h"

namespace flowermachine
{
    /*  The 8x8 grid of the visible page (ARCHITECTURE.md section 6). Cells fill the
        area; a timer at UI_REFRESH_HZ polls every cart for changes; audio files
        dragged from Explorer onto a cell are assigned to it.
    */
    class CartGrid : public juce::Component,
                     public juce::FileDragAndDropTarget,
                     private juce::Timer
    {
    public:
        CartGrid (Controller&, const CartEngine&);
        ~CartGrid() override;

        void setPage (int page);
        int getPage() const noexcept { return page; }

        void resized() override;
        void paintOverChildren (juce::Graphics&) override;

        bool isInterestedInFileDrag (const juce::StringArray& files) override;
        void fileDragEnter (const juce::StringArray& files, int x, int y) override;
        void fileDragMove (const juce::StringArray& files, int x, int y) override;
        void fileDragExit (const juce::StringArray& files) override;
        void filesDropped (const juce::StringArray& files, int x, int y) override;

    private:
        void timerCallback() override;
        int cellAt (int x, int y) const;
        void setHoverCell (int cellIndex);
        static bool isAudioFile (const juce::String& path);

        Controller& controller;
        juce::OwnedArray<CartButton> cells;   // CARTS_PER_PAGE, row-major
        int page = 0;
        int hoverCell = -1;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CartGrid)
    };
}

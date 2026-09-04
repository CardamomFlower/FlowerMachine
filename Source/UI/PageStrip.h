#pragma once

#include <JuceHeader.h>

#include <functional>

namespace flowermachine
{
    /*  The page tabs (ARCHITECTURE.md section 6). Right-click a tab for Rename /
        Remove; the "+" button lives in MainComponent next to the strip.
    */
    class PageStrip : public juce::TabbedButtonBar
    {
    public:
        PageStrip();

        /** Rebuilds the tabs without firing onPageSelected. */
        void setPages (const juce::StringArray& names, int current);

        std::function<void (int page)> onPageSelected;
        std::function<void (int page)> onRenameRequested;
        std::function<void (int page)> onRemoveRequested;
        std::function<void (int page)> onFillRequested;

    private:
        void currentTabChanged (int newCurrentTabIndex, const juce::String& newCurrentTabName) override;
        void popupMenuClickOnTab (int tabIndex, const juce::String& tabName) override;

        bool rebuilding = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PageStrip)
    };
}

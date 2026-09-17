#pragma once

#include <JuceHeader.h>

#include <functional>

namespace flowermachine
{
    /*  The page tabs (ARCHITECTURE.md section 6). Right-click a tab for Rename / Fill / Remove;
        the "+" button and the two scroll arrows live in MainComponent beside the strip.

        The strip lays its own tab buttons out, with a horizontal scroll offset, instead of
        letting TabbedButtonBar do it. The base class has no scrolling: when the tabs do not fit
        it squeezes them to 70 percent and then hides the rest behind a chevron whose menu can
        only select a page - a hidden tab has no button on screen, so its right-click menu, and
        with it Rename, is unreachable. Overriding resized() without calling the base keeps every
        tab laid out and the chevron is never built.
    */
    class PageStrip : public juce::TabbedButtonBar
    {
    public:
        PageStrip();

        /** Rebuilds the tabs without firing onPageSelected. The caller lays the strip out and
            then calls showCurrentTab, because its width may have changed in between. */
        void setPages (const juce::StringArray& names, int current);

        /** Scrolls the selected page into view. */
        void showCurrentTab();

        /** Width the tabs need at this height. More than the strip is given means it scrolls,
            and the caller is expected to make room for the arrows. */
        int getNaturalWidth (int depth) const;

        bool canScrollLeft() const noexcept  { return scrollOffset > 0; }
        bool canScrollRight() const noexcept;

        /** One tab left (-1) or right (+1). */
        void scrollByTabs (int direction);

        std::function<void (int page)> onPageSelected;
        std::function<void (int page)> onRenameRequested;
        std::function<void (int page)> onRemoveRequested;
        std::function<void (int page)> onFillRequested;
        std::function<void()> onScrollChanged;   // the arrows may need enabling or disabling

    private:
        void resized() override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        void currentTabChanged (int newCurrentTabIndex, const juce::String& newCurrentTabName) override;
        void popupMenuClickOnTab (int tabIndex, const juce::String& tabName) override;
        juce::TabBarButton* createTabButton (const juce::String& name, int index) override;

        int tabOverlap (int depth) const;
        int maxScrollOffset() const noexcept;
        void setScrollOffset (int newOffset);
        void showTab (int index);

        int scrollOffset = 0;
        int contentWidth = 0;
        bool rebuilding = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PageStrip)
    };
}

#pragma once

#include <JuceHeader.h>

#include <functional>

namespace flowermachine
{
    /*  The page tabs. Right-click a tab for Rename / Fill / Remove;
        the "+" button and the two scroll arrows live in MainComponent beside the strip.

        The strip lays its own tab buttons out, with a horizontal scroll offset, instead of
        letting TabbedButtonBar do it. The base class has no scrolling: when the tabs do not fit
        it squeezes them to 70 percent and then hides the rest behind a chevron whose menu can
        only select a page - a hidden tab has no button on screen, so its right-click menu, and
        with it Rename, is unreachable. Overriding resized() without calling the base keeps every
        tab laid out and the chevron is never built.

        While the layout is being edited a tab can be dragged along the strip to move its page;
        near either end the strip scrolls under the pointer, so a page can reach a place that
        is out of view.
    */
    class PageStrip : public juce::TabbedButtonBar,
                      public juce::DragAndDropTarget,
                      private juce::Timer
    {
    public:
        PageStrip();
        ~PageStrip() override;

        void setEditMode (bool shouldEdit);
        bool isEditMode() const noexcept { return editMode; }

        /** What a tab hands to the drag-and-drop container, and the page it names (-1 when the
            description is not a tab's). */
        static juce::var dragDescription (int page);
        static int draggedPage (const juce::var& description);

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
        std::function<void (int from, int to)> onPageMoved;
        std::function<void()> onScrollChanged;   // the arrows may need enabling or disabling

        bool isInterestedInDragSource (const SourceDetails&) override;
        void itemDragEnter (const SourceDetails&) override;
        void itemDragMove (const SourceDetails&) override;
        void itemDragExit (const SourceDetails&) override;
        void itemDropped (const SourceDetails&) override;

    private:
        void paintOverChildren (juce::Graphics&) override;
        void timerCallback() override;

        /** The gap between tabs nearest to `x`: 0 is before the first tab, getNumTabs() after
            the last. */
        int insertionIndexAt (int x) const;
        void setInsertionIndex (int index);
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
        bool editMode = false;
        int draggingPage = -1;      // the tab being dragged over the strip, -1 when none
        int insertionIndex = -1;    // where it would land, -1 when nowhere new

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PageStrip)
    };
}

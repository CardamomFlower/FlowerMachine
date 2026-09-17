#include "PageStrip.h"

#include "../Constants.h"

namespace flowermachine
{

namespace
{
    // Pixels per wheel notch. One Windows notch reports a deltaY of about 0.23, so this is
    // roughly a third of a tab: enough to feel like scrolling, small enough to aim with.
    constexpr float wheelPixelsPerUnit = 200.0f;

    /*  TabbedButtonBar hands each button an overlap so the pills it draws can sit on top of one
        another without stealing each other's clicks. That value is set only by the base class's
        own layout, which this strip replaces, so the button carries a setter and the strip
        writes it - otherwise the clickable area of every tab would run under its neighbour.
    */
    class PageTabButton : public juce::TabBarButton
    {
    public:
        PageTabButton (const juce::String& name, juce::TabbedButtonBar& bar)
            : juce::TabBarButton (name, bar) {}

        void setOverlap (int pixels) { overlapPixels = pixels; }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PageTabButton)
    };
}

//==============================================================================
PageStrip::PageStrip()
    : juce::TabbedButtonBar (juce::TabbedButtonBar::TabsAtTop)
{
}

juce::TabBarButton* PageStrip::createTabButton (const juce::String& name, int)
{
    return new PageTabButton (name, *this);
}

void PageStrip::setPages (const juce::StringArray& names, int current)
{
    rebuilding = true;

    clearTabs();

    const auto colour = findColour (juce::ResizableWindow::backgroundColourId);

    for (const auto& name : names)
        addTab (name, colour, -1);

    setCurrentTabIndex (juce::jlimit (0, juce::jmax (0, names.size() - 1), current), false);

    rebuilding = false;
}

void PageStrip::showCurrentTab()
{
    showTab (getCurrentTabIndex());
}

//==============================================================================
int PageStrip::tabOverlap (int depth) const
{
    auto& lf = getLookAndFeel();
    return lf.getTabButtonOverlap (depth) + lf.getTabButtonSpaceAroundImage() * 2;
}

int PageStrip::getNaturalWidth (int depth) const
{
    if (getNumTabs() == 0 || depth <= 0)
        return 0;

    // The same arithmetic TabbedButtonBar::updateTabPositions uses, so the strip is the width
    // the base class would have given it had everything fitted.
    const int overlap = tabOverlap (depth);
    int total = juce::jmax (0, overlap);

    for (int i = 0; i < getNumTabs(); ++i)
        if (auto* tab = getTabButton (i))
            total += tab->getBestTabLength (depth) - overlap;

    return total;
}

int PageStrip::maxScrollOffset() const noexcept
{
    return juce::jmax (0, contentWidth - getWidth());
}

void PageStrip::setScrollOffset (int newOffset)
{
    const int clamped = juce::jlimit (0, maxScrollOffset(), newOffset);

    if (clamped == scrollOffset)
        return;

    scrollOffset = clamped;
    resized();
}

void PageStrip::resized()
{
    const int depth = getHeight();

    if (depth <= 0)
        return;

    const int overlap = tabOverlap (depth);
    contentWidth = getNaturalWidth (depth);

    // Clamped here rather than at each caller: every route that can change the content width or
    // the strip width - a rename, a page added or removed, the window resized, a scheme change -
    // arrives at resized(), and only here is both width known.
    scrollOffset = juce::jlimit (0, maxScrollOffset(), scrollOffset);

    int x = -scrollOffset;
    juce::TabBarButton* frontTab = nullptr;

    for (int i = 0; i < getNumTabs(); ++i)
    {
        auto* tab = getTabButton (i);

        if (tab == nullptr)
            continue;

        const int best = tab->getBestTabLength (depth);

        if (auto* pageTab = dynamic_cast<PageTabButton*> (tab))
            pageTab->setOverlap (juce::jmax (0, overlap / 2));

        tab->setBounds (x, 0, best, depth);
        tab->setVisible (true);   // nothing is ever hidden: every tab keeps its right-click menu
        tab->toBack();

        if (i == getCurrentTabIndex())
            frontTab = tab;

        x += best - overlap;
    }

    if (frontTab != nullptr)
        frontTab->toFront (false);

    if (onScrollChanged != nullptr)
        onScrollChanged();
}

//==============================================================================
bool PageStrip::canScrollRight() const noexcept
{
    return scrollOffset < maxScrollOffset();
}

void PageStrip::scrollByTabs (int direction)
{
    const int depth = getHeight();

    if (depth <= 0 || direction == 0)
        return;

    const int overlap = tabOverlap (depth);
    int left = 0;

    // Walk the tab boundaries and stop at the first one on the far side of where we are.
    for (int i = 0; i < getNumTabs(); ++i)
    {
        auto* tab = getTabButton (i);

        if (tab == nullptr)
            continue;

        if (direction > 0 && left > scrollOffset)
        {
            setScrollOffset (left);
            return;
        }

        if (direction < 0 && left >= scrollOffset && i > 0)
        {
            if (auto* previous = getTabButton (i - 1))
                setScrollOffset (left - (previous->getBestTabLength (depth) - overlap));

            return;
        }

        left += tab->getBestTabLength (depth) - overlap;
    }

    // Past the last boundary: go to whichever end was asked for.
    setScrollOffset (direction > 0 ? maxScrollOffset() : 0);
}

void PageStrip::showTab (int index)
{
    const int depth = getHeight();

    if (depth <= 0 || ! juce::isPositiveAndBelow (index, getNumTabs()))
        return;

    const int overlap = tabOverlap (depth);
    int left = 0;

    for (int i = 0; i < index; ++i)
        if (auto* tab = getTabButton (i))
            left += tab->getBestTabLength (depth) - overlap;

    auto* tab = getTabButton (index);

    if (tab == nullptr)
        return;

    const int width = tab->getBestTabLength (depth);

    if (left < scrollOffset)
        setScrollOffset (left);
    else if (left + width > scrollOffset + getWidth())
        setScrollOffset (left + width - getWidth());
}

void PageStrip::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    // A trackpad reports the horizontal axis; a wheel only the vertical one, and on a strip
    // that is what the operator means by "along".
    const float delta = wheel.deltaX != 0.0f ? -wheel.deltaX : wheel.deltaY;
    setScrollOffset (scrollOffset - juce::roundToInt (delta * wheelPixelsPerUnit));
}

//==============================================================================
void PageStrip::currentTabChanged (int newCurrentTabIndex, const juce::String&)
{
    if (rebuilding || newCurrentTabIndex < 0)
        return;

    // The callback rebuilds the strip from the model, so nothing may touch this object's own
    // state after it: the caller lays the strip out and calls showCurrentTab when it is done.
    if (onPageSelected != nullptr)
        onPageSelected (newCurrentTabIndex);
}

void PageStrip::popupMenuClickOnTab (int tabIndex, const juce::String&)
{
    juce::PopupMenu menu;
    menu.addItem (1, "Rename page...");
    menu.addItem (3, "Fill page from folder...");
    menu.addSeparator();
    menu.addItem (2, "Remove page...", getNumTabs() > 1);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (getTabButton (tabIndex)),
                        [safe = juce::Component::SafePointer<PageStrip> (this), tabIndex] (int result)
                        {
                            if (safe == nullptr)
                                return;

                            if (result == 1 && safe->onRenameRequested != nullptr)
                                safe->onRenameRequested (tabIndex);
                            else if (result == 2 && safe->onRemoveRequested != nullptr)
                                safe->onRemoveRequested (tabIndex);
                            else if (result == 3 && safe->onFillRequested != nullptr)
                                safe->onFillRequested (tabIndex);
                        });
}

} // namespace flowermachine

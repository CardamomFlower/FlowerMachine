#include "PageStrip.h"

#include "../Constants.h"
#include "Palette.h"

namespace flowermachine
{

namespace
{
    // Pixels per wheel notch. One Windows notch reports a deltaY of about 0.23, so this is
    // roughly a third of a tab: enough to feel like scrolling, small enough to aim with.
    constexpr float wheelPixelsPerUnit = 200.0f;

    // Marks a drag as one of this strip's tabs, and which page it carries.
    constexpr const char* pageDragPrefix = "flowermachine-page:";

    // A tab dragged this close to either end of the strip scrolls it a tab at a time, at this
    // pace: slow enough to stop on the page wanted, quick enough to cross sixteen.
    constexpr int autoScrollZone = 28;
    constexpr int autoScrollIntervalMs = 350;

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

        // While the layout is being edited a tab can be picked up and dropped elsewhere on the
        // strip. Selecting a page is a click, which the base class completes on release, so a
        // drag that ends away from the tab selects nothing.
        void mouseDrag (const juce::MouseEvent& e) override
        {
            juce::TabBarButton::mouseDrag (e);

            const auto* strip = dynamic_cast<const PageStrip*> (&owner);

            if (strip == nullptr || ! strip->isEditMode() || ! e.mods.isLeftButtonDown()
                || ! e.mouseWasDraggedSinceMouseDown())
                return;

            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
                if (! container->isDragAndDropActive())
                    container->startDragging (PageStrip::dragDescription (getIndex()), this);
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PageTabButton)
    };
}

//==============================================================================
PageStrip::PageStrip()
    : juce::TabbedButtonBar (juce::TabbedButtonBar::TabsAtTop)
{
}

PageStrip::~PageStrip()
{
    stopTimer();
}

void PageStrip::setEditMode (bool shouldEdit)
{
    editMode = shouldEdit;
}

juce::var PageStrip::dragDescription (int page)
{
    return juce::String (pageDragPrefix) + juce::String (page);
}

int PageStrip::draggedPage (const juce::var& description)
{
    const auto text = description.toString();

    if (! text.startsWith (pageDragPrefix))
        return -1;

    const int page = text.fromFirstOccurrenceOf (pageDragPrefix, false, false).getIntValue();
    return juce::isPositiveAndBelow (page, MAX_PAGES) ? page : -1;
}

juce::TabBarButton* PageStrip::createTabButton (const juce::String& name, int)
{
    return new PageTabButton (name, *this);
}

void PageStrip::setPages (const juce::StringArray& names, int current)
{
    // The tabs are about to be replaced, a dragged one with them. A drag whose source has gone
    // ends without telling the strip, so its state is let go of here.
    stopTimer();
    draggingPage = -1;
    insertionIndex = -1;

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
bool PageStrip::isInterestedInDragSource (const SourceDetails& details)
{
    // Only this strip's own tabs: the description alone could have come from anywhere.
    const auto* source = details.sourceComponent.get();
    return editMode && source != nullptr && source->getParentComponent() == this
        && juce::isPositiveAndBelow (draggedPage (details.description), getNumTabs());
}

void PageStrip::itemDragEnter (const SourceDetails& details)
{
    draggingPage = draggedPage (details.description);
    setInsertionIndex (insertionIndexAt (details.localPosition.x));
    startTimer (autoScrollIntervalMs);
}

void PageStrip::itemDragMove (const SourceDetails& details)
{
    setInsertionIndex (insertionIndexAt (details.localPosition.x));
}

void PageStrip::itemDragExit (const SourceDetails&)
{
    stopTimer();
    draggingPage = -1;
    setInsertionIndex (-1);
}

void PageStrip::itemDropped (const SourceDetails& details)
{
    stopTimer();

    const int from = draggedPage (details.description);
    const int gap = insertionIndexAt (details.localPosition.x);

    draggingPage = -1;
    setInsertionIndex (-1);

    // The gaps are counted with the dragged tab still in the strip, and it leaves its own
    // place when it moves, so a gap to its right is one index nearer the start.
    const int to = gap > from ? gap - 1 : gap;

    if (from < 0 || to == from || onPageMoved == nullptr)
        return;

    // Not from in here: moving a page rebuilds every tab, the dragged one among them, and this
    // runs inside the mouse release that ends the drag.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PageStrip> (this), from, to]
    {
        if (safe != nullptr && safe->onPageMoved != nullptr)
            safe->onPageMoved (from, to);
    });
}

int PageStrip::insertionIndexAt (int x) const
{
    for (int i = 0; i < getNumTabs(); ++i)
        if (auto* tab = getTabButton (i); tab != nullptr && x < tab->getBounds().getCentreX())
            return i;

    return getNumTabs();
}

void PageStrip::setInsertionIndex (int index)
{
    // Either side of the dragged tab is where it already is: nothing to mark.
    if (draggingPage >= 0 && (index == draggingPage || index == draggingPage + 1))
        index = -1;

    if (index != insertionIndex)
    {
        insertionIndex = index;
        repaint();
    }
}

void PageStrip::paintOverChildren (juce::Graphics& g)
{
    if (insertionIndex < 0 || getNumTabs() == 0)
        return;

    // Halfway between the pill before the gap and the pill after it; at either end, the edge
    // of the one pill there is.
    const auto pillLeft  = [this] (int i) { auto* t = getTabButton (i); return t->getX() + t->getActiveArea().getX(); };
    const auto pillRight = [this] (int i) { auto* t = getTabButton (i); return t->getX() + t->getActiveArea().getRight(); };

    int x = 0;

    if (insertionIndex == 0)
        x = pillLeft (0);
    else if (insertionIndex >= getNumTabs())
        x = pillRight (getNumTabs() - 1);
    else
        x = (pillRight (insertionIndex - 1) + pillLeft (insertionIndex)) / 2;

    constexpr float barWidth = 3.0f;
    const float centre = juce::jlimit (barWidth * 0.5f, (float) getWidth() - barWidth * 0.5f, (float) x);

    g.setColour (palette::accent);
    g.fillRoundedRectangle (centre - barWidth * 0.5f, 3.0f, barWidth, (float) getHeight() - 6.0f, barWidth * 0.5f);
}

void PageStrip::timerCallback()
{
    // A pointer held still at the end of the strip sends no drag moves, so the scrolling has to
    // come from here.
    const auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this);

    if (draggingPage < 0 || container == nullptr || ! container->isDragAndDropActive())
    {
        stopTimer();
        return;
    }

    const int x = getMouseXYRelative().x;

    if (x < autoScrollZone && canScrollLeft())
        scrollByTabs (-1);
    else if (x > getWidth() - autoScrollZone && canScrollRight())
        scrollByTabs (1);
    else
        return;

    setInsertionIndex (insertionIndexAt (x));
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

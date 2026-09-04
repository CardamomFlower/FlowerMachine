#include "PageStrip.h"

namespace flowermachine
{

PageStrip::PageStrip()
    : juce::TabbedButtonBar (juce::TabbedButtonBar::TabsAtTop)
{
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

void PageStrip::currentTabChanged (int newCurrentTabIndex, const juce::String&)
{
    if (! rebuilding && newCurrentTabIndex >= 0 && onPageSelected != nullptr)
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

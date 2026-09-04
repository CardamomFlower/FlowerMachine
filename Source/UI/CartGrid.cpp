#include "CartGrid.h"

#include "../Constants.h"
#include "Dialogs.h"
#include "Palette.h"

namespace flowermachine
{

CartGrid::CartGrid (Controller& controllerToUse, const CartEngine& engine)
    : controller (controllerToUse)
{
    for (int i = 0; i < CARTS_PER_PAGE; ++i)
        addAndMakeVisible (cells.add (new CartButton (i, controller, engine)));

    startTimerHz (UI_REFRESH_HZ);
}

CartGrid::~CartGrid()
{
    stopTimer();
}

void CartGrid::setPage (int newPage)
{
    page = juce::jlimit (0, MAX_PAGES - 1, newPage);

    for (int i = 0; i < cells.size(); ++i)
        cells[i]->setCartId (Controller::cartIdOf (page, i));
}

void CartGrid::timerCallback()
{
    for (auto* cellButton : cells)
        cellButton->refresh();
}

void CartGrid::resized()
{
    constexpr int gap = 8;

    // Cells fill the whole area (rectangular, like a cart wall); the leftover
    // pixels of the integer division are centred.
    const auto area = getLocalBounds();
    const int cellW = juce::jmax (1, (area.getWidth()  - gap * (GRID_COLS - 1)) / GRID_COLS);
    const int cellH = juce::jmax (1, (area.getHeight() - gap * (GRID_ROWS - 1)) / GRID_ROWS);

    const int totalW = cellW * GRID_COLS + gap * (GRID_COLS - 1);
    const int totalH = cellH * GRID_ROWS + gap * (GRID_ROWS - 1);
    const int x0 = area.getX() + (area.getWidth()  - totalW) / 2;
    const int y0 = area.getY() + (area.getHeight() - totalH) / 2;

    for (int row = 0; row < GRID_ROWS; ++row)
        for (int col = 0; col < GRID_COLS; ++col)
            cells[row * GRID_COLS + col]->setBounds (x0 + col * (cellW + gap),
                                                     y0 + row * (cellH + gap),
                                                     cellW, cellH);
}

void CartGrid::paintOverChildren (juce::Graphics& g)
{
    if (juce::isPositiveAndBelow (hoverCell, cells.size()))
    {
        g.setColour (palette::accent);
        g.drawRoundedRectangle (cells[hoverCell]->getBounds().toFloat().expanded (2.0f), 8.0f, 3.0f);
    }
}

//==============================================================================
bool CartGrid::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& path : files)
        if (isAudioFile (path))
            return true;

    return false;
}

void CartGrid::fileDragEnter (const juce::StringArray&, int x, int y)
{
    setHoverCell (cellAt (x, y));
}

void CartGrid::fileDragMove (const juce::StringArray&, int x, int y)
{
    setHoverCell (cellAt (x, y));
}

void CartGrid::fileDragExit (const juce::StringArray&)
{
    setHoverCell (-1);
}

void CartGrid::filesDropped (const juce::StringArray& files, int x, int y)
{
    const int cellIndex = cellAt (x, y);
    setHoverCell (-1);

    if (cellIndex < 0)
        return;

    juce::File file;

    for (const auto& path : files)
    {
        if (isAudioFile (path))
        {
            file = juce::File (path);
            break;
        }
    }

    if (! file.existsAsFile())
        return;

    const int cartId = Controller::cartIdOf (page, cellIndex);
    const auto& status = controller.getStatus (cartId);

    if (status.isAssigned())
    {
        dialogs::confirm ("Replace cart",
                          "Replace \"" + status.title + "\" with " + file.getFileName() + "?",
                          "Replace",
                          [safe = juce::Component::SafePointer<CartGrid> (this), cartId, file]
                          {
                              if (safe != nullptr)
                                  safe->controller.assignFile (cartId, file);
                          });
        return;
    }

    controller.assignFile (cartId, file);
}

//==============================================================================
int CartGrid::cellAt (int x, int y) const
{
    for (int i = 0; i < cells.size(); ++i)
        if (cells[i]->getBounds().contains (x, y))
            return i;

    return -1;
}

void CartGrid::setHoverCell (int cellIndex)
{
    if (hoverCell != cellIndex)
    {
        hoverCell = cellIndex;
        repaint();
    }
}

bool CartGrid::isAudioFile (const juce::String& path)
{
    static const juce::String extensions = []
    {
        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();
        return formatManager.getWildcardForAllFormats().replace ("*.", "");   // "wav;aiff;..."
    }();

    return juce::File::isAbsolutePath (path) && juce::File (path).hasFileExtension (extensions);
}

} // namespace flowermachine

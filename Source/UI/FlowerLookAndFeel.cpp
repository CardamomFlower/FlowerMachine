#include "FlowerLookAndFeel.h"

#include "Palette.h"

namespace flowermachine
{

FlowerLookAndFeel::FlowerLookAndFeel()
{
    applyPalette();
}

void FlowerLookAndFeel::setTheme (juce::PropertiesFile& settings, palette::Theme theme)
{
    palette::save (settings, theme);
    palette::apply (theme);

    auto& current = juce::Desktop::getInstance().getDefaultLookAndFeel();

    if (auto* ours = dynamic_cast<FlowerLookAndFeel*> (&current))
    {
        ours->applyPalette();

        // Re-publishing the same pointer is what makes every live window, menu and dialog
        // run lookAndFeelChanged() and repaint — including the few components that keep a
        // colour of their own, which override the look-and-feel and would otherwise stay.
        juce::Desktop::getInstance().setDefaultLookAndFeel (ours);
    }
}

void FlowerLookAndFeel::applyPalette()
{
    using namespace juce;

    // First, because it rewrites a great many ids from the scheme; the overrides follow.
    setColourScheme ({
        palette::window,    // windowBackground
        palette::panel,     // widgetBackground
        palette::panel,     // menuBackground
        palette::outline,   // outline
        palette::text,      // defaultText
        palette::control,   // defaultFill
        palette::window,    // highlightedText
        palette::accent,    // highlightedFill
        palette::text       // menuText
    });

    setDefaultSansSerifTypefaceName (palette::fontFamily());

    setColour (TextButton::buttonColourId,   palette::control);
    setColour (TextButton::buttonOnColourId, palette::accent);
    setColour (TextButton::textColourOffId,  palette::text);
    setColour (TextButton::textColourOnId,   palette::window);

    setColour (ComboBox::backgroundColourId, palette::control);
    setColour (ComboBox::outlineColourId,    palette::outline);
    setColour (ComboBox::textColourId,       palette::text);
    setColour (ComboBox::arrowColourId,      palette::text2);

    setColour (Label::textColourId, palette::text);

    setColour (TabbedButtonBar::tabOutlineColourId,   palette::outline);
    setColour (TabbedButtonBar::frontOutlineColourId, palette::accent);
    setColour (TabbedButtonBar::frontTextColourId,    palette::text);
    setColour (TabbedButtonBar::tabTextColourId,      palette::text2);

    setColour (Slider::rotarySliderFillColourId,    palette::accent);
    setColour (Slider::rotarySliderOutlineColourId, palette::outlineStrong);
    setColour (Slider::thumbColourId,               palette::text);

    setColour (PopupMenu::backgroundColourId,            palette::panel);
    setColour (PopupMenu::textColourId,                  palette::text);
    // Not `control`: in the light scheme it sits within a few percent of `panel` and the
    // highlighted item was indistinguishable from the rest of the menu.
    setColour (PopupMenu::highlightedBackgroundColourId, palette::menuHighlight);
    setColour (PopupMenu::highlightedTextColourId,       palette::text);

    setColour (AlertWindow::backgroundColourId, palette::panel);
    setColour (AlertWindow::textColourId,       palette::text);
    setColour (AlertWindow::outlineColourId,    palette::outlineStrong);

    setColour (TextEditor::backgroundColourId,     palette::cartEmpty);
    setColour (TextEditor::textColourId,           palette::text);
    setColour (TextEditor::outlineColourId,        palette::outline);
    setColour (TextEditor::focusedOutlineColourId, palette::accent);
    setColour (TextEditor::highlightColourId,      palette::accent.withAlpha (0.35f));

    setColour (ScrollBar::thumbColourId,           palette::outlineStrong);
    setColour (ListBox::backgroundColourId,        palette::cartEmpty);
    setColour (BubbleComponent::backgroundColourId, palette::panel);
    setColour (BubbleComponent::outlineColourId,   palette::outlineStrong);

    // The gain knob's value bubble does NOT take its text colour from any BubbleComponent
    // id: Slider's popup paints the value with TooltipWindow::textColourId. Left unset,
    // that came from the scheme's highlighted text — near-black on a near-black bubble, so
    // the number was there and simply could not be seen. The tooltip proper shares the id,
    // so both are given the same dark panel and the same cream type.
    setColour (TooltipWindow::backgroundColourId, palette::panel);
    setColour (TooltipWindow::textColourId,       palette::text);
    setColour (TooltipWindow::outlineColourId,    palette::outlineStrong);
    setColour (ToggleButton::textColourId,         palette::text);
    setColour (ToggleButton::tickColourId,         palette::accent);
}

//==============================================================================
juce::Font FlowerLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    if (buttonHeight >= 36)
        return palette::font (16.0f, palette::Weight::bold).withExtraKerningFactor (0.08f);

    if (buttonHeight >= 26)
        return palette::font (13.0f, palette::Weight::semibold);

    return palette::font (11.0f, palette::Weight::semibold).withExtraKerningFactor (0.06f);
}

void FlowerLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                              bool isMouseOverButton, bool isButtonDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const float radius = button.getHeight() >= 26 ? 6.0f : 4.0f;

    auto fill = backgroundColour;

    // Brightening a pale fill does almost nothing, so the light scheme's hover and press
    // went unfelt. Move away from the fill's own brightness instead of always upwards.
    const bool pale = fill.getPerceivedBrightness() > 0.5f;

    if (isButtonDown)
        fill = pale ? fill.darker (0.12f) : fill.brighter (0.15f);
    else if (isMouseOverButton)
        fill = pale ? fill.darker (0.06f) : fill.brighter (0.07f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, radius);

    if (backgroundColour == palette::control)   // chrome buttons carry a hairline; STOP ALL and the strip do not
    {
        g.setColour (palette::controlLine);
        g.drawRoundedRectangle (bounds, radius, 1.0f);
    }
}

//==============================================================================
int FlowerLookAndFeel::getTabButtonBestWidth (juce::TabBarButton& button, int)
{
    const auto font = palette::font (14.0f, palette::Weight::semibold);
    return juce::GlyphArrangement::getStringWidthInt (font, button.getButtonText()) + 32;
}

void FlowerLookAndFeel::drawTabButton (juce::TabBarButton& button, juce::Graphics& g, bool isMouseOver, bool)
{
    const auto area = button.getActiveArea();
    const bool front = button.isFrontTab();

    if (front)
    {
        g.setColour (palette::tabActive);
        g.fillRoundedRectangle (area.toFloat(), 6.0f);
        g.fillRect (area.withTop (area.getBottom() - 6));   // square bottom corners
        g.setColour (palette::accent);
        g.fillRect (area.withTop (area.getBottom() - 2));
    }
    else if (isMouseOver)
    {
        g.setColour (palette::tabActive.withAlpha (0.6f));
        g.fillRoundedRectangle (area.toFloat(), 6.0f);
    }

    g.setColour (front ? palette::text : palette::text2);
    g.setFont (palette::font (14.0f, palette::Weight::semibold));
    g.drawText (button.getButtonText(), area, juce::Justification::centred, false);
}

void FlowerLookAndFeel::drawTabbedButtonBarBackground (juce::TabbedButtonBar&, juce::Graphics&) {}
void FlowerLookAndFeel::drawTabAreaBehindFrontButton (juce::TabbedButtonBar&, juce::Graphics&, int, int) {}

//==============================================================================
void FlowerLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                          float rotaryStartAngle, float rotaryEndAngle, juce::Slider&)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto circle = bounds.withSizeKeepingCentre (size, size).reduced (0.5f);

    g.setColour (palette::knob);
    g.fillEllipse (circle);
    g.setColour (palette::outlineStrong);
    g.drawEllipse (circle, 1.0f);

    const float radius = circle.getWidth() * 0.5f;
    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.0f, -radius + 2.0f, 2.0f, radius * 0.7f, 1.0f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (circle.getCentreX(), circle.getCentreY()));

    g.setColour (palette::text);
    g.fillPath (pointer);
}

//==============================================================================
juce::Font FlowerLookAndFeel::getPopupMenuFont()               { return palette::font (14.0f); }
juce::Font FlowerLookAndFeel::getComboBoxFont (juce::ComboBox&) { return palette::font (14.0f); }
juce::Font FlowerLookAndFeel::getAlertWindowTitleFont()        { return palette::font (17.0f, palette::Weight::semibold); }
juce::Font FlowerLookAndFeel::getAlertWindowMessageFont()      { return palette::font (14.0f); }
juce::Font FlowerLookAndFeel::getAlertWindowFont()             { return palette::font (13.0f); }

} // namespace flowermachine

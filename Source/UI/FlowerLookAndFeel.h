#pragma once

#include <JuceHeader.h>

#include "Palette.h"

namespace flowermachine
{
    /*  Shared look for every stock widget (ARCHITECTURE.md section 6), after the
        design canvas: flat rounded controls, tabs with an accent underline, a
        plain knob with a cream pointer. Colours and fonts come from Palette.h.
    */
    class FlowerLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        FlowerLookAndFeel();

        /** Rewrites every colour from whatever scheme `palette` currently holds.
            The tables here are copies, so they do not follow a palette change by themselves. */
        void applyPalette();

        /** Message thread: switch the scheme, remember it, and repaint everything on screen -
            including the components that cache a colour of their own. */
        static void setTheme (juce::PropertiesFile&, palette::Theme);

        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                                   bool isMouseOverButton, bool isButtonDown) override;

        int getTabButtonBestWidth (juce::TabBarButton&, int tabDepth) override;
        void drawTabButton (juce::TabBarButton&, juce::Graphics&, bool isMouseOver, bool isMouseDown) override;
        void drawTabbedButtonBarBackground (juce::TabbedButtonBar&, juce::Graphics&) override;
        void drawTabAreaBehindFrontButton (juce::TabbedButtonBar&, juce::Graphics&, int w, int h) override;

        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                               float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

        juce::Font getPopupMenuFont() override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        juce::Font getAlertWindowTitleFont() override;
        juce::Font getAlertWindowMessageFont() override;
        juce::Font getAlertWindowFont() override;

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FlowerLookAndFeel)
    };
}

#pragma once

#include <JuceHeader.h>

#include <array>

/*  The FlowerMachine look, in two schemes.

    The names below started as constants and are now references onto one live scheme, so the
    eighty-odd places that say `palette::accent` did not have to change: assigning a new
    scheme writes through the same storage and everything that paints afterwards sees it.

    What does NOT follow by itself is anything that copied a colour once — the LookAndFeel's
    tables and the handful of per-component setColour calls. Those are re-applied from
    FlowerLookAndFeel::applyPalette and from each component's lookAndFeelChanged().

    Dark came from the design canvas (direction A, "Studio panel"). Light keeps every role
    the same: one amber accent for whatever is playing, red for STOP ALL and for trouble.
*/
namespace flowermachine::palette
{
    struct Scheme
    {
        juce::Colour window, panel, panelLine;
        juce::Colour cart, cartEmpty, cartEmptyLine, cartLoading, cartPlaying;
        juce::Colour cartMissing, missingLine, missingText;
        juce::Colour outline, outlineStrong, control, controlLine, cartControl, knob;
        juce::Colour text, text2, text3;
        juce::Colour accent, accentBright, danger, dangerText, tabActive;
        juce::Colour menuHighlight;   // must read clearly against `panel` in both schemes
    };

    inline Scheme darkScheme()
    {
        return {
            juce::Colour (0xff1c1a18), juce::Colour (0xff24211e), juce::Colour (0xff33302b),
            juce::Colour (0xff2e2a26), juce::Colour (0xff211f1c), juce::Colour (0xff2a2724),
            juce::Colour (0xff262320), juce::Colour (0xff5b4220),
            juce::Colour (0xff3a2523), juce::Colour (0xff8a3a33), juce::Colour (0xffe08a80),
            juce::Colour (0xff3d3833), juce::Colour (0xff57504a), juce::Colour (0xff33302b),
            juce::Colour (0xff47423c), juce::Colour (0xff3b3631), juce::Colour (0xff1f1b17),
            juce::Colour (0xffede6d8), juce::Colour (0xffa59c8f), juce::Colour (0xff6f675e),
            juce::Colour (0xffe9a23b), juce::Colour (0xfff5c664), juce::Colour (0xffd94a3d),
            juce::Colour (0xfffff4ee), juce::Colour (0xff2a2622),
            juce::Colour (0xff433d36)
        };
    }

    inline Scheme lightScheme()
    {
        return {
            juce::Colour (0xfff4f1ea), juce::Colour (0xffe9e4d9), juce::Colour (0xffd3ccbe),
            juce::Colour (0xfffbf9f4), juce::Colour (0xffe8e3d8), juce::Colour (0xffd8d1c2),
            juce::Colour (0xfff0ece3), juce::Colour (0xffffe3ad),
            juce::Colour (0xfff7dcd8), juce::Colour (0xffc9736a), juce::Colour (0xff8f3529),
            juce::Colour (0xffcfc7b8), juce::Colour (0xffa99f8d), juce::Colour (0xffe2dccf),
            juce::Colour (0xffc6bdab), juce::Colour (0xffddd6c7), juce::Colour (0xfff7f4ed),
            juce::Colour (0xff241f19), juce::Colour (0xff6a6154), juce::Colour (0xff9a9184),
            juce::Colour (0xffc8791a), juce::Colour (0xff8f5209), juce::Colour (0xffc8342a),
            juce::Colour (0xfffff4ee), juce::Colour (0xffffffff),
            juce::Colour (0xffcfc5b1)
        };
    }

    enum class Theme { dark, light };

    /** The one live scheme. Written by apply(), read through the references below. */
    inline Scheme active = darkScheme();

    inline const juce::Colour& window        = active.window;
    inline const juce::Colour& panel         = active.panel;          // top bar, status line
    inline const juce::Colour& panelLine     = active.panelLine;      // chrome dividers
    inline const juce::Colour& cart          = active.cart;           // ready cart fill
    inline const juce::Colour& cartEmpty     = active.cartEmpty;
    inline const juce::Colour& cartEmptyLine = active.cartEmptyLine;
    inline const juce::Colour& cartLoading   = active.cartLoading;
    inline const juce::Colour& cartPlaying   = active.cartPlaying;
    inline const juce::Colour& cartMissing   = active.cartMissing;
    inline const juce::Colour& missingLine   = active.missingLine;
    inline const juce::Colour& missingText   = active.missingText;
    inline const juce::Colour& outline       = active.outline;        // carts
    inline const juce::Colour& outlineStrong = active.outlineStrong;
    inline const juce::Colour& control       = active.control;        // File, Settings, +
    inline const juce::Colour& controlLine   = active.controlLine;
    inline const juce::Colour& cartControl   = active.cartControl;    // Stop, Loop
    inline const juce::Colour& knob          = active.knob;
    inline const juce::Colour& text          = active.text;
    inline const juce::Colour& text2         = active.text2;
    inline const juce::Colour& text3         = active.text3;
    inline const juce::Colour& accent        = active.accent;         // playing, progress, active tab, Loop lit
    inline const juce::Colour& accentBright  = active.accentBright;   // remaining time while playing
    inline const juce::Colour& danger        = active.danger;         // STOP ALL only
    inline const juce::Colour& dangerText    = active.dangerText;
    inline const juce::Colour& tabActive     = active.tabActive;
    inline const juce::Colour& menuHighlight = active.menuHighlight;

    inline void apply (Theme theme)
    {
        active = theme == Theme::light ? lightScheme() : darkScheme();
    }

    //==============================================================================
    // Persistence, in the shape Renderer.h already uses.
    inline constexpr const char* settingsKey = "theme";

    inline Theme load (const juce::PropertiesFile& settings)
    {
        return settings.getValue (settingsKey, "dark") == "light" ? Theme::light : Theme::dark;
    }

    inline void save (juce::PropertiesFile& settings, Theme theme)
    {
        settings.setValue (settingsKey, theme == Theme::light ? "light" : "dark");
    }

    //==============================================================================
    /** Offered first in the colour picker; any colour is allowed. These are the operator's
        own categories, so they do not follow the scheme. */
    inline const std::array<juce::Colour, 5> bandPresets {
        juce::Colour (0xff4e7fc4), juce::Colour (0xff3f8f8a), juce::Colour (0xff4f9a5b),
        juce::Colour (0xff8a63b8), juce::Colour (0xffc0703f)
    };

    enum class Weight { regular, semibold, bold };

    /** Segoe UI: settled with the owner on 2026-09-04 after comparing it with Barlow at the
        real sizes. It is on every Windows 10, so nothing has to be embedded or licensed. */
    inline const juce::String& fontFamily()
    {
        static const juce::String family ("Segoe UI");
        return family;
    }

    inline juce::Font font (float height, Weight weight = Weight::regular)
    {
        static const juce::StringArray styles = juce::Font::findAllTypefaceStyles (fontFamily());

        const juce::String style = weight == Weight::bold     ? "Bold"
                                 : weight == Weight::semibold ? (styles.contains ("Semibold") ? "Semibold" : "Bold")
                                                              : "Regular";

        return juce::Font (juce::FontOptions (fontFamily(), style, height));
    }
}

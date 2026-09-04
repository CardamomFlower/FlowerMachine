#pragma once

#include <JuceHeader.h>

#include <array>

/*  The FlowerMachine look, as settled on the design canvas "FlowerMachine Look"
    (Spec artboard): warm charcoal from the Cardamom panel material, cream type,
    one amber accent for whatever is playing, red only for STOP ALL and trouble.
    The only place in the code that knows these values.
*/
namespace flowermachine::palette
{
    inline const juce::Colour window        { 0xff1c1a18 };
    inline const juce::Colour panel         { 0xff24211e };   // top bar, status line
    inline const juce::Colour panelLine     { 0xff33302b };   // chrome dividers
    inline const juce::Colour cart          { 0xff2e2a26 };   // ready cart fill
    inline const juce::Colour cartEmpty     { 0xff211f1c };
    inline const juce::Colour cartEmptyLine { 0xff2a2724 };
    inline const juce::Colour cartLoading   { 0xff262320 };
    inline const juce::Colour cartPlaying   { 0xff5b4220 };
    inline const juce::Colour cartMissing   { 0xff3a2523 };
    inline const juce::Colour missingLine   { 0xff8a3a33 };
    inline const juce::Colour missingText   { 0xffe08a80 };
    inline const juce::Colour outline       { 0xff3d3833 };   // carts
    inline const juce::Colour outlineStrong { 0xff57504a };
    inline const juce::Colour control       { 0xff33302b };   // File, Settings, +
    inline const juce::Colour controlLine   { 0xff47423c };
    inline const juce::Colour cartControl   { 0xff3b3631 };   // STOP, LOOP
    inline const juce::Colour knob          { 0xff1f1b17 };
    inline const juce::Colour text          { 0xffede6d8 };
    inline const juce::Colour text2         { 0xffa59c8f };
    inline const juce::Colour text3         { 0xff6f675e };
    inline const juce::Colour accent        { 0xffe9a23b };   // playing, progress, active tab, LOOP lit, dirty dot
    inline const juce::Colour accentBright  { 0xfff5c664 };   // remaining time while playing
    inline const juce::Colour danger        { 0xffd94a3d };   // STOP ALL only
    inline const juce::Colour dangerText    { 0xfffff4ee };
    inline const juce::Colour tabActive     { 0xff2a2622 };

    /** Offered first in the colour picker; any colour is allowed. */
    inline const std::array<juce::Colour, 5> bandPresets {
        juce::Colour (0xff4e7fc4), juce::Colour (0xff3f8f8a), juce::Colour (0xff4f9a5b),
        juce::Colour (0xff8a63b8), juce::Colour (0xffc0703f)
    };

    enum class Weight { regular, semibold, bold };

    /** Barlow once it is embedded (D-table, section 9); Segoe UI until then. */
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

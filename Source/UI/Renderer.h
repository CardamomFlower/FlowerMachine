#pragma once

#include <JuceHeader.h>

/*  Renderer preference (ARCHITECTURE.md section 7).

    JUCE 8 opens Windows windows with Direct2D; the GDI software renderer is the
    fallback for an old GPU driver on the work PC. The choice is persisted in the
    settings; `--software-renderer` on the command line overrides it for one run.
*/
namespace flowermachine::renderer
{
    enum class Choice { direct2D, software };

    inline bool forcedSoftware = false;              // set once, in Main, from the command line
    inline constexpr const char* settingsKey = "renderer";

    inline Choice load (const juce::PropertiesFile& settings)
    {
        return settings.getValue (settingsKey, "direct2d") == "software" ? Choice::software
                                                                          : Choice::direct2D;
    }

    inline void save (juce::PropertiesFile& settings, Choice choice)
    {
        settings.setValue (settingsKey, choice == Choice::software ? "software" : "direct2d");
    }

    inline Choice effective (const juce::PropertiesFile& settings)
    {
        return forcedSoftware ? Choice::software : load (settings);
    }

    /** Applies the choice to one native window. */
    inline void applyToPeer (juce::ComponentPeer& peer, Choice choice)
    {
        const auto engines = peer.getAvailableRenderingEngines();

        if (engines.size() < 2)
            return;   // a single engine: nothing to choose

        // Windows lists { "Software Renderer", "Direct2D" }: match by name, fall back to position.
        int index = engines.indexOf (choice == Choice::software ? "Software Renderer" : "Direct2D");

        if (index < 0)
            index = (choice == Choice::software ? 0 : 1);

        if (peer.getCurrentRenderingEngine() != index)
            peer.setCurrentRenderingEngine (index);
    }

    /** Applies the choice to a component that already owns a native window. */
    inline void applyTo (juce::Component& topLevel, Choice choice)
    {
        if (auto* peer = topLevel.getPeer())
            applyToPeer (*peer, choice);
    }

    /** Applies the choice to every native window that exists right now — menus, dialogs and
        call-out boxes included, which the desktop component list does not cover. */
    inline void applyToAllWindows (Choice choice)
    {
        for (int i = 0; i < juce::ComponentPeer::getNumPeers(); ++i)
            if (auto* peer = juce::ComponentPeer::getPeer (i))
                applyToPeer (*peer, choice);
    }

    /*  Keeps every window on the chosen renderer.

        JUCE creates each new peer with Direct2D and offers no global default, so applying
        the choice once at startup leaves every menu, dialog and colour picker on Direct2D.
        On the work PC that is exactly the case the software switch exists for: the main
        window would paint and nothing else would. A slow poll is the only way to catch
        windows the app does not create itself; applyToPeer is a no-op for a peer that is
        already right, so the cost is a handful of integer compares.
    */
    class Enforcer : private juce::Timer
    {
    public:
        explicit Enforcer (const juce::PropertiesFile& settingsToRead) : settings (settingsToRead) {}

        void start()
        {
            timerCallback();
            startTimer (500);
        }

    private:
        void timerCallback() override { applyToAllWindows (effective (settings)); }

        const juce::PropertiesFile& settings;
    };
}

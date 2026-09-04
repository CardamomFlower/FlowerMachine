#pragma once

#include <JuceHeader.h>

#include <functional>
#include <memory>

#include "../Control/Controller.h"
#include "../Engine/AudioEngine.h"
#include "CartGrid.h"
#include "PageStrip.h"
#include "Renderer.h"

namespace flowermachine
{
    /*  Main window content (ARCHITECTURE.md section 6): top bar with the File menu,
        the page strip, the grid and the status line. Owns the preset workflow:
        new / open / save / save as / recent, unsaved-changes prompts, reopening the
        last preset at launch.
    */
    class MainComponent : public juce::Component,
                          public juce::KeyListener
    {
    public:
        MainComponent (Controller&, AudioEngine&, juce::PropertiesFile&);
        ~MainComponent() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        /*  Two routes to the same shortcuts. A key press is delivered to the focused
            component and then walks UP its parents, so with nothing focused it reaches the
            window and stops there — this component is a child and would never see it. As a
            key listener registered on the window it is on that chain either way.
        */
        bool keyPressed (const juce::KeyPress&) override;
        bool keyPressed (const juce::KeyPress&, juce::Component* originatingComponent) override;

        /** Deals with unsaved changes (save / discard) and then runs `proceed`; nothing on cancel. */
        void requestClose (std::function<void()> proceed);

        /** Writes the preset name into the window caption. Public because the caption can
            only be set once the component has a parent, i.e. after setContentOwned. */
        void refreshTitle();

    private:
        bool handleShortcut (const juce::KeyPress&);
        void showFileMenu();
        void newPreset();
        void openPreset();
        void openFile (const juce::File&);
        void save (std::function<void()> then = nullptr);
        void saveAs (std::function<void()> then = nullptr);
        void confirmDiscard (std::function<void()> proceed);
        void rememberPreset (const juce::File&);

        void addPage();
        void renamePage (int page);
        void removePage (int page);
        void fillPage (int page);

        void rebuildPages();
        void updateStatus();
        void writePreset (const juce::File&, std::function<void()> then);

        Controller& controller;
        AudioEngine& engine;
        juce::PropertiesFile& settings;

        juce::TextButton fileButton { "File" };
        juce::Label presetLabel;
        juce::TextButton settingsButton { "Settings" };
        juce::TextButton stopAllButton { "STOP ALL" };
        PageStrip pageStrip;
        juce::TextButton addPageButton { "+" };
        CartGrid grid;
        juce::Label statusLabel;

        juce::RecentlyOpenedFilesList recentFiles;
        std::unique_ptr<juce::FileChooser> chooser;
        juce::Rectangle<int> tabRowBounds;   // for the underline in paint()
        juce::TooltipWindow tooltipWindow { this, 700 };
        renderer::Enforcer rendererEnforcer { settings };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
    };
}

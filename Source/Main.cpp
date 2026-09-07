#include <JuceHeader.h>

#include "Control/Controller.h"
#include "Model/Preset.h"
#include "Engine/AudioEngine.h"
#include "Loading/SampleLoader.h"
#include "UI/FlowerLookAndFeel.h"
#include "UI/MainComponent.h"
#include "UI/Renderer.h"
#include "UI/SettingsDialog.h"

namespace flowermachine
{
    class FlowerMachineApplication : public juce::JUCEApplication
    {
    public:
        FlowerMachineApplication() = default;

        const juce::String getApplicationName() override    { return ProjectInfo::projectName; }
        const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
        bool moreThanOneInstanceAllowed() override          { return false; }

        void initialise (const juce::String& commandLine) override
        {
            // Emergency switch for a window that does not paint at all (ARCHITECTURE.md section 7).
            renderer::forcedSoftware = commandLine.contains ("--software-renderer");

            const auto startupPreset = presetFromCommandLine (commandLine);

            juce::PropertiesFile::Options options;
            options.applicationName = ProjectInfo::projectName;
            options.filenameSuffix = "settings";
            options.folderName = "CardamomTools/FlowerMachine";
            options.osxLibrarySubFolder = "Application Support";
            appProperties.setStorageParameters (options);

            auto& settings = *appProperties.getUserSettings();

            // Before the look-and-feel is published: its tables are copies of the palette,
            // and the constructor has already read the compiled-in dark scheme.
            palette::apply (palette::load (settings));
            lookAndFeel.applyPalette();

            juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
            engine = std::make_unique<AudioEngine> (settings);
            loader = std::make_unique<SampleLoader>();
            controller = std::make_unique<Controller> (*engine, *loader);
            mainWindow = std::make_unique<MainWindow> (getApplicationName(), *controller, *engine, settings,
                                                       startupPreset);
        }

        void shutdown() override
        {
            // The Settings window is owned by the modal manager, not by us, and JUCE would
            // delete it after this function has freed the engine it points at.
            SettingsComponent::closeIfOpen();

            // Reverse order of construction: the window uses the controller, the
            // controller uses loader and engine, the loader waits for its jobs.
            mainWindow = nullptr;
            controller = nullptr;
            loader = nullptr;
            engine = nullptr;
            juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
            appProperties.saveIfNeeded();
            appProperties.closeFiles();
        }

        /*  Every quit request lands here — the window's close button, Windows logging off or
            restarting, the taskbar. Asking about unsaved changes in one place is what keeps a
            scheduled restart from throwing away an unsaved preset.
        */
        /*  Double-clicking a .fmpreset while the program is already running. Only one instance
            is allowed, so Windows hands the file to this one instead of starting another.
        */
        void anotherInstanceStarted (const juce::String& commandLine) override
        {
            if (mainWindow == nullptr)
                return;

            // Whatever it was launched for. Someone who clicks the shortcut again means
            // "show me the window", and there is only ever the one.
            mainWindow->toFront (true);

            const auto file = presetFromCommandLine (commandLine);

            if (! file.existsAsFile())
                return;

            if (auto* main = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
                main->openPresetFile (file);
        }

        void systemRequestedQuit() override
        {
            if (quitRequested)
                return;   // the prompt is already up

            if (mainWindow != nullptr)
            {
                if (auto* main = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
                {
                    quitRequested = true;
                    main->requestClose ([this] { quit(); });
                    quitRequested = false;   // cancelled: the next request must prompt again
                    return;
                }
            }

            quit();
        }

    private:
        /*  The path Windows hands over when a .fmpreset is double-clicked. The registered
            command is `"<exe>" "%1"`, but the switch below can be on the line as well and in
            either order, so every token is examined rather than just the first: the answer is
            the first one that really is a preset on disk.
        */
        static juce::File presetFromCommandLine (const juce::String& commandLine)
        {
            for (const auto& token : juce::StringArray::fromTokens (commandLine, " ", "\"'"))
            {
                const auto text = token.unquoted().trim();

                if (! juce::File::isAbsolutePath (text))
                    continue;

                if (const juce::File file (text);
                    file.existsAsFile() && file.hasFileExtension (Preset::fileExtension))
                    return file;
            }

            return {};
        }

        class MainWindow : public juce::DocumentWindow
        {
        public:
            MainWindow (const juce::String& name, Controller& controllerToUse, AudioEngine& engineToUse,
                        juce::PropertiesFile& settingsToUse, const juce::File& presetToOpen)
                : DocumentWindow (name,
                                  juce::Desktop::getInstance().getDefaultLookAndFeel()
                                      .findColour (juce::ResizableWindow::backgroundColourId),
                                  allButtons),
                  settings (settingsToUse)
            {
                setUsingNativeTitleBar (true);
                setContentOwned (new MainComponent (controllerToUse, engineToUse, settingsToUse, presetToOpen), true);
                setResizable (true, false);
                setResizeLimits (WINDOW_MIN_W, WINDOW_MIN_H, 10000, 10000);

                const auto savedState = settings.getValue (windowStateKey);

                if (savedState.isEmpty() || ! restoreWindowStateFromString (savedState))
                    centreWithSize (getWidth(), getHeight());

                // Both paths above can leave the window bigger than the screen or hanging off
                // it, and neither is constrained: centreWithSize writes raw bounds, and a state
                // saved on a large monitor is only nudged, never shrunk. On the work PC that put
                // the title bar above the top edge with nothing left to grab.
                fitToDisplay();

                setVisible (true);
                renderer::applyTo (*this, renderer::effective (settings));

                if (auto* main = dynamic_cast<MainComponent*> (getContentComponent()))
                {
                    // The caption can only be written once the content has a parent, so the
                    // preset reopened during the constructor above has not set it yet.
                    main->refreshTitle();

                    // Esc and the file shortcuts have to work whatever holds the focus —
                    // including nothing, which is the state right after launch.
                    addKeyListener (main);
                    main->grabKeyboardFocus();
                }
            }

            ~MainWindow() override
            {
                settings.setValue (windowStateKey, getWindowStateAsString());
            }

            void closeButtonPressed() override
            {
                juce::JUCEApplication::getInstance()->systemRequestedQuit();
            }

            void lookAndFeelChanged() override
            {
                // The window keeps its own background colour, so a scheme change has to be
                // read back from the look-and-feel rather than from the component.
                setBackgroundColour (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
                DocumentWindow::lookAndFeelChanged();
            }

        private:
            /** Shrinks and nudges the window until it fits, whole, on the screen it is on -
                and does nothing at all to a window that already fits. */
            void fitToDisplay()
            {
                /*  A window closed maximised comes back maximised, and setBounds would quietly
                    un-maximise it: ComponentPeer::updateBounds reports "not full screen" and
                    Windows drops the state. The maximised shape needs no correcting - but the
                    size it restores DOWN to still does, or the first click on the restore
                    button hands back the very window this function exists to prevent.
                */
                if (isFullScreen())
                {
                    setFullScreen (false);
                    fitToDisplay();
                    setFullScreen (true);
                    return;
                }

                const auto& displays = juce::Desktop::getInstance().getDisplays();

                // The native caption and border sit OUTSIDE these bounds, so the space they
                // need has to be accounted for before anything is compared.
                juce::BorderSize<int> frame;

                if (auto* peer = getPeer())
                    if (const auto peerFrame = peer->getFrameSizeIfPresent())
                        frame = *peerFrame;

                // Leave alone anything that is already wholly on the desktop, whatever shape the
                // desktop is. Clamping to one display would pull a window deliberately spanning
                // two of them onto one, and shrink it again on every launch.
                if (displays.getRectangleList (true).containsRectangle (frame.addedTo (getBounds())))
                    return;

                const auto* display = displays.getDisplayForRect (getBounds());

                if (display == nullptr)
                    display = displays.getPrimaryDisplay();

                if (display == nullptr)
                    return;

                const auto available = frame.subtractedFrom (display->userArea);

                const auto fitted = getBounds()
                                        .withWidth  (juce::jmin (getWidth(),  available.getWidth()))
                                        .withHeight (juce::jmin (getHeight(), available.getHeight()))
                                        .constrainedWithin (available);

                setBounds (fitted);
            }

            static constexpr const char* windowStateKey = "windowState";

            juce::PropertiesFile& settings;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
        };

        FlowerLookAndFeel lookAndFeel;
        juce::ApplicationProperties appProperties;
        bool quitRequested = false;
        std::unique_ptr<AudioEngine> engine;
        std::unique_ptr<SampleLoader> loader;
        std::unique_ptr<Controller> controller;
        std::unique_ptr<MainWindow> mainWindow;
    };
}

START_JUCE_APPLICATION (flowermachine::FlowerMachineApplication)

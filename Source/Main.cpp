#include <JuceHeader.h>

#include "Control/Controller.h"
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

            juce::PropertiesFile::Options options;
            options.applicationName = ProjectInfo::projectName;
            options.filenameSuffix = "settings";
            options.folderName = "CardamomTools/FlowerMachine";
            options.osxLibrarySubFolder = "Application Support";
            appProperties.setStorageParameters (options);

            juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

            auto& settings = *appProperties.getUserSettings();
            engine = std::make_unique<AudioEngine> (settings);
            loader = std::make_unique<SampleLoader>();
            controller = std::make_unique<Controller> (*engine, *loader);
            mainWindow = std::make_unique<MainWindow> (getApplicationName(), *controller, *engine, settings);
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
        class MainWindow : public juce::DocumentWindow
        {
        public:
            MainWindow (const juce::String& name, Controller& controllerToUse, AudioEngine& engineToUse,
                        juce::PropertiesFile& settingsToUse)
                : DocumentWindow (name,
                                  juce::Desktop::getInstance().getDefaultLookAndFeel()
                                      .findColour (juce::ResizableWindow::backgroundColourId),
                                  allButtons),
                  settings (settingsToUse)
            {
                setUsingNativeTitleBar (true);
                setContentOwned (new MainComponent (controllerToUse, engineToUse, settingsToUse), true);
                setResizable (true, false);
                setResizeLimits (720, 540, 10000, 10000);

                const auto savedState = settings.getValue (windowStateKey);

                if (savedState.isEmpty() || ! restoreWindowStateFromString (savedState))
                    centreWithSize (getWidth(), getHeight());

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

        private:
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

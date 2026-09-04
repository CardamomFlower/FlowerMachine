#include <JuceHeader.h>

#include "Cracktro.h"
#include "InstallerConstants.h"

namespace flowerinstall
{
    class InstallerApplication : public juce::JUCEApplication
    {
    public:
        InstallerApplication() = default;

        const juce::String getApplicationName() override    { return "FlowerMachine Setup"; }
        const juce::String getApplicationVersion() override { return APP_VERSION; }
        bool moreThanOneInstanceAllowed() override          { return false; }

        void initialise (const juce::String&) override
        {
            // getCommandLineParameterArray goes through CommandLineToArgvW, so a quoted
            // path with spaces arrives in one piece.
            const auto arguments = getCommandLineParameterArray();
            const auto mode = arguments.contains ("--uninstall") ? Cracktro::Mode::uninstall
                                                                 : Cracktro::Mode::install;

            mainWindow = std::make_unique<MainWindow> (getApplicationName(), mode);
        }

        void shutdown() override
        {
            mainWindow = nullptr;
        }

        void systemRequestedQuit() override
        {
            if (mainWindow != nullptr)
                mainWindow->beginClosing();
            else
                quit();
        }

    private:
        /*  Closing is not immediate: the chiptune fades out first, because cutting an
            audio callback dead is exactly the click the house rules exist to prevent.
            The window asks the screen to fade, then polls until it says it is done.
        */
        class MainWindow : public juce::DocumentWindow,
                           private juce::Timer
        {
        public:
            MainWindow (const juce::String& name, Cracktro::Mode mode)
                : DocumentWindow (name, juce::Colour (0xff0b0a18), closeButton)
            {
                setUsingNativeTitleBar (true);
                screen = new Cracktro (mode);
                setContentOwned (screen, true);
                setResizable (false, false);
                centreWithSize (getWidth(), getHeight());
                setVisible (true);
                toFront (true);
            }

            void closeButtonPressed() override
            {
                beginClosing();
            }

            void beginClosing()
            {
                if (screen != nullptr)
                {
                    screen->beginShutdown();
                    startTimerHz (30);

                    // Never hang on a fade that cannot finish.
                    giveUpAt = juce::Time::getMillisecondCounter() + 900;
                }
                else
                {
                    juce::JUCEApplication::getInstance()->quit();
                }
            }

        private:
            void timerCallback() override
            {
                if ((screen != nullptr && screen->readyToClose())
                     || juce::Time::getMillisecondCounter() > giveUpAt)
                {
                    stopTimer();
                    juce::JUCEApplication::getInstance()->quit();
                }
            }

            juce::Component::SafePointer<Cracktro> screen;
            juce::uint32 giveUpAt = 0;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
        };

        juce::LookAndFeel_V4 lookAndFeel;
        std::unique_ptr<MainWindow> mainWindow;
    };
}

START_JUCE_APPLICATION (flowerinstall::InstallerApplication)

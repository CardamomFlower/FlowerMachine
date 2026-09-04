#pragma once

#include <JuceHeader.h>

#include <atomic>

#include "Payload.h"

namespace flowerinstall
{
    /*  Does the install on its own thread, so the cracktro keeps running.

        Everything the screen reads is an atomic; the result String is only touched by
        the message thread once `isDone()` is true. No locks, no callbacks into the UI.
    */
    class InstallTask : public juce::Thread
    {
    public:
        struct Options
        {
            bool startMenuShortcut = true;
            bool desktopShortcut = true;
            bool associatePresets = true;
        };

        explicit InstallTask (const Options&);
        ~InstallTask() override;

        void run() override;

        //==============================================================================
        // Read from the message thread while it runs
        float getProgress() const noexcept { return progress.load(); }
        int getStepIndex() const noexcept  { return step.load(); }
        bool isDone() const noexcept       { return done.load(); }

        /** What the current step is called; safe to call any time. */
        static juce::String describeStep (int index);
        static int getNumSteps() noexcept;

        //==============================================================================
        // Valid once isDone()
        bool succeeded() const noexcept        { return ok; }
        const juce::String& getMessage() const { return message; }
        const juce::File& getInstalledExe() const { return installedExe; }

    private:
        void setStep (int index);
        bool fail (const juce::String& why);

        const Options options;
        Payload payload;

        std::atomic<float> progress { 0.0f };
        std::atomic<int> step { 0 };
        std::atomic<bool> done { false };

        bool ok = false;
        juce::String message;
        juce::File installedExe;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstallTask)
    };
}

#pragma once

#include <JuceHeader.h>

#include <atomic>

namespace flowerinstall
{
    /*  The other half, behind --uninstall.

        The awkward part is that the uninstaller is running from inside the folder it
        has to delete. Windows refuses to DELETE a running image but is happy to RENAME
        or MOVE one, so this moves itself out to the temp folder first, deletes the tree,
        and leaves a detached batch file behind to sweep up its own image afterwards.
        MoveFileEx with MOVEFILE_DELAY_UNTIL_REBOOT is not an option: it needs an
        elevated token, and this process must never elevate.
    */
    class UninstallTask : public juce::Thread
    {
    public:
        struct Options
        {
            /** The operator's presets and settings. Off unless they say otherwise. */
            bool removePersonalData = false;
        };

        explicit UninstallTask (const Options&);
        ~UninstallTask() override;

        void run() override;

        float getProgress() const noexcept { return progress.load(); }
        int getStepIndex() const noexcept  { return step.load(); }
        bool isDone() const noexcept       { return done.load(); }

        static juce::String describeStep (int index);
        static int getNumSteps() noexcept;

        bool succeeded() const noexcept        { return ok; }
        const juce::String& getMessage() const { return message; }

    private:
        void setStep (int index);
        bool fail (const juce::String& why);
        static void leaveJanitorBehind (const juce::File& imageToDelete);

        const Options options;

        std::atomic<float> progress { 0.0f };
        std::atomic<int> step { 0 };
        std::atomic<bool> done { false };

        bool ok = false;
        juce::String message;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UninstallTask)
    };
}

#include "UninstallTask.h"

#include "InstallerConstants.h"
#include "RegistryEntries.h"
#include "Shortcuts.h"

namespace flowerinstall
{

namespace
{
    const char* const stepNames[] =
    {
        "CHECKING",
        "STEPPING ASIDE",
        "REMOVING THE PROGRAM",
        "REMOVING SHORTCUTS",
        "UNREGISTERING",
        "DONE"
    };

    constexpr int numSteps = (int) (sizeof (stepNames) / sizeof (stepNames[0]));

    bool flowerMachineSeemsToBeRunning()
    {
        juce::InterProcessLock lock (APP_LOCK_NAME);

        if (lock.enter (0))
        {
            lock.exit();
            return false;
        }

        return true;
    }

    void removeShortcut (const juce::File& folder)
    {
        if (folder != juce::File())
            folder.getChildFile (SHORTCUT_NAME).deleteFile();
    }
}

//==============================================================================
UninstallTask::UninstallTask (const Options& optionsToUse)
    : juce::Thread ("FlowerMachine uninstall"),
      options (optionsToUse)
{
}

UninstallTask::~UninstallTask()
{
    stopThread (4000);
}

juce::String UninstallTask::describeStep (int index)
{
    return stepNames[juce::jlimit (0, numSteps - 1, index)];
}

int UninstallTask::getNumSteps() noexcept
{
    return numSteps;
}

void UninstallTask::setStep (int index)
{
    step.store (index);
    progress.store ((float) index / (float) (numSteps - 1));
}

bool UninstallTask::fail (const juce::String& why)
{
    ok = false;
    message = why;
    done.store (true);
    return false;
}

//==============================================================================
void UninstallTask::leaveJanitorBehind (const juce::File& imageToDelete)
{
    // A detached shell that waits for this process to let go of its own image, deletes
    // it, then deletes itself. Absolute path to cmd.exe: JUCE's ChildProcess resolves a
    // bare name against the current directory and PATH.
    const auto batch = juce::File::getSpecialLocation (juce::File::tempDirectory)
                           .getChildFile ("flowermachine-cleanup.cmd");

    const auto target = imageToDelete.getFullPathName();

    juce::String script;
    script << "@echo off\r\n"
           << "for /l %%i in (1,1,40) do (\r\n"
           << "  del /f /q \"" << target << "\" >nul 2>&1\r\n"
           << "  if not exist \"" << target << "\" goto gone\r\n"
           << "  ping -n 2 127.0.0.1 >nul\r\n"
           << ")\r\n"
           << ":gone\r\n"
           << "del /f /q \"%~f0\" >nul 2>&1\r\n";

    if (! batch.replaceWithText (script))
        return;

    const auto cmd = juce::File::getSpecialLocation (juce::File::windowsSystemDirectory)
                         .getChildFile ("cmd.exe");

    juce::ChildProcess sweeper;
    sweeper.start (juce::StringArray { cmd.getFullPathName(), "/c", batch.getFullPathName() }, 0);
}

void UninstallTask::run()
{
    setStep (0);

    if (flowerMachineSeemsToBeRunning())
    {
        fail ("FlowerMachine is running. Close it and start this again.");
        return;
    }

    const auto self = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    auto folder = registry::previousInstallFolder();

    if (folder == juce::File() || ! folder.isDirectory())
        folder = self.getParentDirectory();

    //==============================================================================
    setStep (1);

    // Explorer starts a double-clicked exe with its own folder as the working directory,
    // and a process holds a handle on that: without this the files go but the folder stays.
    juce::File::getSpecialLocation (juce::File::tempDirectory).setAsCurrentWorkingDirectory();

    juce::File asideImage;

    if (self.isAChildOf (folder))
    {
        asideImage = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("FlowerMachineUninstall-" + juce::String (juce::Time::getMillisecondCounter()) + ".tmp");

        if (! self.moveFileTo (asideImage))
        {
            fail ("Could not move the uninstaller out of the way. Nothing was removed.");
            return;
        }
    }

    //==============================================================================
    setStep (2);
    folder.setReadOnly (false, true);
    folder.deleteRecursively();

    if (folder.exists())
    {
        // Leave the Settings entry alone so it can be run again.
        fail ("Some files in " + folder.getFullPathName() + " could not be removed.");

        if (asideImage != juce::File())
            leaveJanitorBehind (asideImage);

        return;
    }

    //==============================================================================
    setStep (3);
    removeShortcut (shortcuts::startMenuProgramsFolder());
    removeShortcut (juce::File::getSpecialLocation (juce::File::userDesktopDirectory));

    //==============================================================================
    setStep (4);
    registry::removeAll();
    shortcuts::notifyAssociationsChanged();

    juce::String note;

    if (options.removePersonalData)
    {
        // Only the app's own settings folder - CardamomTools above it is shared with
        // the other tools and must survive.
        settingsFolder().deleteRecursively();

        const auto presets = presetsFolder();

        // Documents is very often redirected into OneDrive, where "remove" would take
        // the station's presets off every device that syncs the account.
        const auto oneDrive = juce::SystemStats::getEnvironmentVariable ("OneDrive", {});
        const bool inCloud = oneDrive.isNotEmpty() && presets.getFullPathName().startsWithIgnoreCase (oneDrive);

        if (inCloud)
            note = " YOUR PRESETS WERE LEFT IN " + presets.getFullPathName().toUpperCase() + " (CLOUD FOLDER).";
        else
            presets.getParentDirectory().deleteRecursively();
    }
    else
    {
        note = " YOUR PRESETS AND SETTINGS WERE LEFT ALONE.";
    }

    //==============================================================================
    setStep (5);

    if (asideImage != juce::File())
        leaveJanitorBehind (asideImage);

    ok = true;
    message = "FLOWERMACHINE HAS BEEN REMOVED." + note;
    done.store (true);
}

} // namespace flowerinstall

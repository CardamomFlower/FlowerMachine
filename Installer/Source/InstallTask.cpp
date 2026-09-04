#include "InstallTask.h"

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
        "WRITING FLOWERMACHINE",
        "WRITING THE UNINSTALLER",
        "MAKING SHORTCUTS",
        "REGISTERING",
        "DONE"
    };

    constexpr int numSteps = (int) (sizeof (stepNames) / sizeof (stepNames[0]));

    /** True when FlowerMachine is running for this user: JUCE holds a named mutex keyed
        on the application name for the app's whole lifetime. Cannot see another logon
        session, so the write itself stays the real gate. */
    bool flowerMachineSeemsToBeRunning()
    {
        juce::InterProcessLock lock (APP_LOCK_NAME);

        if (lock.enter (0))
        {
            lock.exit();
            return false;
        }

        return true;   // taken, or Windows would not say — both mean "do not overwrite"
    }
}

//==============================================================================
InstallTask::InstallTask (const Options& optionsToUse)
    : juce::Thread ("FlowerMachine install"),
      options (optionsToUse)
{
}

InstallTask::~InstallTask()
{
    stopThread (4000);
}

juce::String InstallTask::describeStep (int index)
{
    return stepNames[juce::jlimit (0, numSteps - 1, index)];
}

int InstallTask::getNumSteps() noexcept
{
    return numSteps;
}

void InstallTask::setStep (int index)
{
    step.store (index);
    progress.store ((float) index / (float) (numSteps - 1));
}

bool InstallTask::fail (const juce::String& why)
{
    ok = false;
    message = why;
    done.store (true);
    return false;
}

//==============================================================================
void InstallTask::run()
{
    setStep (0);

    if (! payload.isValid())
    {
        fail (payload.getError());
        return;
    }

    if (flowerMachineSeemsToBeRunning())
    {
        fail ("FlowerMachine is running. Close it and start this again.");
        return;
    }

    const auto folder = installFolder();

    if (! folder.createDirectory())
    {
        fail ("Could not create " + folder.getFullPathName());
        return;
    }

    // A previous run may have had to move the running exe aside; it can go now.
    folder.getChildFile ("FlowerMachine_old.exe").deleteFile();

    //==============================================================================
    setStep (1);
    installedExe = folder.getChildFile (EXE_NAME);

    if (const auto written = payload.writeTo (installedExe); written.failed())
    {
        fail (written.getErrorMessage());
        return;
    }

    if (threadShouldExit())
        return;

    //==============================================================================
    setStep (2);
    const auto uninstaller = folder.getChildFile (UNINSTALL_EXE);
    const auto self = juce::File::getSpecialLocation (juce::File::currentExecutableFile);

    if (self != uninstaller && ! self.copyFileTo (uninstaller))
    {
        fail ("Could not put the uninstaller in " + folder.getFullPathName());
        return;
    }

    // The icon travels with the installer so shortcuts and Settings > Apps have one.
    const auto icon = folder.getChildFile (ICON_NAME);
    icon.replaceWithData (BinaryData::FlowerMachine_ico, (size_t) BinaryData::FlowerMachine_icoSize);

    if (threadShouldExit())
        return;

    //==============================================================================
    setStep (3);
    juce::StringArray shortcutProblems;

    if (options.startMenuShortcut)
    {
        const auto programs = shortcuts::startMenuProgramsFolder();

        if (programs == juce::File())
            shortcutProblems.add ("Windows would not say where the Start Menu is");
        else if (const auto r = shortcuts::create (programs.getChildFile (SHORTCUT_NAME),
                                                   installedExe, "Cart wall for radio", icon); r.failed())
            shortcutProblems.add (r.getErrorMessage());
    }

    if (options.desktopShortcut)
    {
        const auto desktop = juce::File::getSpecialLocation (juce::File::userDesktopDirectory);

        if (const auto r = shortcuts::create (desktop.getChildFile (SHORTCUT_NAME),
                                              installedExe, "Cart wall for radio", icon); r.failed())
            shortcutProblems.add (r.getErrorMessage());
    }

    if (threadShouldExit())
        return;

    //==============================================================================
    setStep (4);
    registry::writeUninstallEntry (folder, folder.getNumberOfChildFiles (juce::File::findFiles) > 0
                                              ? payload.getLength() + self.getSize()
                                              : payload.getLength());

    if (options.associatePresets)
    {
        registry::writeFileAssociation (installedExe, icon);
        shortcuts::notifyAssociationsChanged();
    }

    if (threadShouldExit())
        return;

    // Deliberately nothing under Documents: a fresh install opens an empty cart wall,
    // and the app creates its own presets folder the first time you save one.

    //==============================================================================
    setStep (5);
    ok = true;
    message = shortcutProblems.isEmpty()
                  ? "INSTALLED IN " + folder.getFullPathName().toUpperCase()
                  : "INSTALLED, BUT: " + shortcutProblems.joinIntoString ("; ");
    done.store (true);
}

} // namespace flowerinstall

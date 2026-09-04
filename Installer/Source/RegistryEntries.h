#pragma once

#include <JuceHeader.h>

namespace flowerinstall::registry
{
    /*  Everything lives under HKEY_CURRENT_USER, so none of it needs elevation.

        Two things are written:

        - the entry that puts FlowerMachine in Settings > Apps, with the command that
          runs the uninstaller;
        - the .fmpreset association, so a preset opens by double-click.

        juce::WindowsRegistry::registerFileAssociation is deliberately not used: it
        writes the open command with an unquoted path, which breaks the moment the
        install folder has a space in it — and %LOCALAPPDATA% does, for many users.
    */

    /** @param installFolder  where the program was installed
        @param sizeInBytes    what to report as the installed size */
    void writeUninstallEntry (const juce::File& installFolder, juce::int64 sizeInBytes);

    /** @param exe   the installed FlowerMachine.exe
        @param icon  the icon to show for preset files; File() falls back to the exe */
    void writeFileAssociation (const juce::File& exe, const juce::File& icon = {});

    /** Removes both, and nothing else. Safe to call when they were never written. */
    void removeAll();

    /** True when this machine already has FlowerMachine registered for this user. */
    bool isInstalled();

    /** Where a previous run installed to, or File() if there is no record. */
    juce::File previousInstallFolder();
}

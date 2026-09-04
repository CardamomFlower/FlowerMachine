#pragma once

#include <JuceHeader.h>

namespace flowerinstall::shortcuts
{
    /** The per-user Start Menu "Programs" folder, following folder redirection.

        JUCE has no SpecialLocationType for it, and composing it from %APPDATA% by hand
        is wrong under redirection, so this asks the shell. Returns File() if the shell
        will not say. */
    juce::File startMenuProgramsFolder();

    /** Writes a .lnk.

        juce::File::createShortcut exists but sets only the path and the description —
        no working directory, no icon, no show command — and leaks a COM apartment
        reference per call, so this does the IShellLink work directly.

        @param linkFile     the .lnk to create, parent directories included
        @param target       what it points at (absolute)
        @param description  the tooltip; the visible name is the .lnk file name
        @param iconFile     where to take the icon from; pass File() to use the target
    */
    juce::Result create (const juce::File& linkFile,
                         const juce::File& target,
                         const juce::String& description,
                         const juce::File& iconFile = {});

    /** Tells Explorer that file associations changed, so icons refresh without a logoff. */
    void notifyAssociationsChanged();
}

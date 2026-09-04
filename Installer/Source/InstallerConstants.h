#pragma once

#include <JuceHeader.h>

/*  Everything the installer and the uninstaller must agree on, in one place.

    A per-user install (decision of 2026-09-04): nothing here needs administrator
    rights, nothing writes outside HKEY_CURRENT_USER and the user's own folders,
    and the process must never be elevated — a UAC prompt would repoint
    %LOCALAPPDATA% at the administrator's profile and install to the wrong place.
*/
namespace flowerinstall
{
    inline constexpr const char* APP_NAME       = "FlowerMachine";
    inline constexpr const char* APP_VERSION    = "0.1.0";   // must match FlowerMachine.jucer
    inline constexpr const char* PUBLISHER      = "Cardamom Tools";
    inline constexpr const char* WEBSITE        = "https://github.com/CardamomFlower/Flower";

    inline constexpr const char* EXE_NAME       = "FlowerMachine.exe";
    inline constexpr const char* UNINSTALL_EXE  = "Uninstall FlowerMachine.exe";
    inline constexpr const char* SHORTCUT_NAME  = "FlowerMachine.lnk";
    inline constexpr const char* ICON_NAME      = "FlowerMachine.ico";

    /** JUCE keys its single-instance mutex on the application NAME, so this is how the
        installer can tell whether FlowerMachine is running. Must stay equal to the
        `name` attribute of FlowerMachine.jucer. */
    inline constexpr const char* APP_LOCK_NAME  = "juceAppLock_FlowerMachine";

    inline constexpr const char* PRESET_EXTENSION = ".fmpreset";
    inline constexpr const char* PROG_ID          = "CardamomTools.FlowerMachine.Preset";
    inline constexpr const char* PROG_ID_LABEL    = "FlowerMachine preset";

    // HKEY_CURRENT_USER paths. juce::WindowsRegistry takes the hive as part of the string.
    inline constexpr const char* HKCU = "HKEY_CURRENT_USER\\";

    inline juce::String uninstallKey()
    {
        return juce::String (HKCU) + "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + APP_NAME;
    }

    inline juce::String classesKey()
    {
        return juce::String (HKCU) + "Software\\Classes\\";
    }

    /** %LOCALAPPDATA%\Programs\FlowerMachine.

        Local, not roaming: a 7 MB program image in roaming AppData would be dragged
        through profile sync on a managed station PC. */
    inline juce::File installFolder()
    {
        return juce::File::getSpecialLocation (juce::File::windowsLocalAppData)
                   .getChildFile ("Programs").getChildFile (APP_NAME);
    }

    /** The operator's own data. Never removed without being asked. */
    inline juce::File presetsFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile (APP_NAME).getChildFile ("Presets");
    }

    /** Only this folder — CardamomTools above it is shared with the other tools. */
    inline juce::File settingsFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("CardamomTools").getChildFile (APP_NAME);
    }
}

#include "RegistryEntries.h"

#include "InstallerConstants.h"

namespace flowerinstall::registry
{

namespace
{
    using Reg = juce::WindowsRegistry;

    /*  deleteKey and deleteValue CREATE the key on the way to failing to delete it, so
        every removal is guarded. keyExists appends its own trailing backslash, so it
        wants the same unterminated form deleteKey does. */
    void removeKey (const juce::String& path)
    {
        if (Reg::keyExists (path))
            Reg::deleteKey (path);
    }

    void removeValue (const juce::String& path)
    {
        if (Reg::valueExists (path))
            Reg::deleteValue (path);
    }

    juce::String progIdKey()
    {
        return classesKey() + PROG_ID;
    }

    juce::String extensionKey()
    {
        return classesKey() + PRESET_EXTENSION;
    }
}

//==============================================================================
void writeUninstallEntry (const juce::File& folder, juce::int64 sizeInBytes)
{
    const auto key = uninstallKey() + "\\";
    const auto uninstaller = folder.getChildFile (UNINSTALL_EXE).getFullPathName();
    const auto icon = folder.getChildFile (ICON_NAME);

    Reg::setValue (key + "DisplayName",     juce::String (APP_NAME));
    Reg::setValue (key + "DisplayVersion",  juce::String (APP_VERSION));
    Reg::setValue (key + "Publisher",       juce::String (PUBLISHER));
    Reg::setValue (key + "URLInfoAbout",    juce::String (WEBSITE));
    Reg::setValue (key + "InstallLocation", folder.getFullPathName());
    Reg::setValue (key + "DisplayIcon",     icon.existsAsFile() ? icon.getFullPathName()
                                                                : folder.getChildFile (EXE_NAME).getFullPathName());

    // Quoted: %LOCALAPPDATA% contains the user name, which routinely has a space in it.
    Reg::setValue (key + "UninstallString", "\"" + uninstaller + "\" --uninstall");

    Reg::setValue (key + "EstimatedSize", (juce::uint32) juce::jmax ((juce::int64) 1, sizeInBytes / 1024));
    Reg::setValue (key + "NoModify", (juce::uint32) 1);
    Reg::setValue (key + "NoRepair", (juce::uint32) 1);
}

void writeFileAssociation (const juce::File& exe, const juce::File& icon)
{
    const auto prog = progIdKey() + "\\";

    Reg::setValue (prog, juce::String (PROG_ID_LABEL));
    Reg::setValue (prog + "DefaultIcon\\", (icon.existsAsFile() ? icon.getFullPathName()
                                                                : exe.getFullPathName()) + ",0");
    Reg::setValue (prog + "shell\\open\\command\\", "\"" + exe.getFullPathName() + "\" \"%1\"");

    // The extension points at the ProgID, and also offers it in "Open with".
    Reg::setValue (extensionKey() + "\\", juce::String (PROG_ID));
    Reg::setValue (extensionKey() + "\\OpenWithProgids\\" + PROG_ID, juce::String());

    // So the app shows under a readable name rather than the bare exe in Open With.
    const auto applications = classesKey() + "Applications\\" + EXE_NAME + "\\";
    Reg::setValue (applications + "FriendlyAppName", juce::String (APP_NAME));
    Reg::setValue (applications + "shell\\open\\command\\", "\"" + exe.getFullPathName() + "\" \"%1\"");
    Reg::setValue (applications + "SupportedTypes\\" + PRESET_EXTENSION, juce::String());
}

void removeAll()
{
    removeKey (uninstallKey());
    removeKey (progIdKey());
    removeKey (classesKey() + "Applications\\" + EXE_NAME);

    // The extension key may be shared with whatever else claimed it later, so only the
    // parts that are ours go.
    if (Reg::getValue (extensionKey() + "\\") == PROG_ID)
        removeValue (extensionKey() + "\\");

    removeValue (extensionKey() + "\\OpenWithProgids\\" + PROG_ID);
}

bool isInstalled()
{
    return Reg::valueExists (uninstallKey() + "\\InstallLocation");
}

juce::File previousInstallFolder()
{
    const auto path = Reg::getValue (uninstallKey() + "\\InstallLocation");
    return juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
}

} // namespace flowerinstall::registry

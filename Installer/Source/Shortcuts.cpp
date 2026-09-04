#include "WindowsGlue.h"

#include "Shortcuts.h"

namespace flowerinstall::shortcuts
{

namespace
{
    /*  CoInitializeEx per call, undone on the way out. The installer does its COM work
        from a worker thread, and JUCE only initialises COM for the message thread. */
    struct ScopedApartment
    {
        ScopedApartment()  { hr = CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED); }
        ~ScopedApartment() { if (SUCCEEDED (hr)) CoUninitialize(); }

        bool usable() const noexcept { return SUCCEEDED (hr) || hr == RPC_E_CHANGED_MODE; }

        HRESULT hr = E_FAIL;
    };
}

juce::File startMenuProgramsFolder()
{
    ScopedApartment com;

    PWSTR path = nullptr;

    if (SUCCEEDED (SHGetKnownFolderPath (FOLDERID_Programs, KF_FLAG_CREATE, nullptr, &path)) && path != nullptr)
    {
        const juce::File folder { juce::String (path) };   // braces: parentheses here declare a function
        CoTaskMemFree (path);
        return folder;
    }

    if (path != nullptr)
        CoTaskMemFree (path);

    // Older shells, or a policy that blocks the known-folder call.
    WCHAR buffer[MAX_PATH] = {};

    if (SHGetSpecialFolderPathW (nullptr, buffer, CSIDL_PROGRAMS, TRUE))
        return juce::File { juce::String (buffer) };

    return {};
}

juce::Result create (const juce::File& linkFile,
                     const juce::File& target,
                     const juce::String& description,
                     const juce::File& iconFile)
{
    if (! target.existsAsFile())
        return juce::Result::fail (target.getFullPathName() + " is not there to link to.");

    if (! linkFile.getParentDirectory().createDirectory())
        return juce::Result::fail ("Could not create " + linkFile.getParentDirectory().getFullPathName());

    ScopedApartment com;

    if (! com.usable())
        return juce::Result::fail ("Windows would not start COM for the shortcut.");

    IShellLinkW* link = nullptr;

    if (FAILED (CoCreateInstance (CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IShellLinkW, (void**) &link)) || link == nullptr)
        return juce::Result::fail ("Windows would not create the shortcut object.");

    // Keep every owning object alive for the whole call sequence: a wide pointer taken
    // from a temporary File or String dies at the semicolon.
    const auto targetPath  = target.getFullPathName();
    const auto workingDir  = target.getParentDirectory().getFullPathName();
    const auto iconPath    = iconFile != juce::File() ? iconFile.getFullPathName() : targetPath;

    link->SetPath (targetPath.toWideCharPointer());
    link->SetWorkingDirectory (workingDir.toWideCharPointer());
    link->SetDescription (description.toWideCharPointer());
    link->SetIconLocation (iconPath.toWideCharPointer(), 0);
    link->SetShowCmd (SW_SHOWNORMAL);

    IPersistFile* persist = nullptr;
    juce::Result result = juce::Result::ok();

    if (SUCCEEDED (link->QueryInterface (IID_IPersistFile, (void**) &persist)) && persist != nullptr)
    {
        const auto linkPath = linkFile.getFullPathName();

        if (FAILED (persist->Save (linkPath.toWideCharPointer(), TRUE)))
            result = juce::Result::fail ("Could not write " + linkFile.getFullPathName());

        persist->Release();
    }
    else
    {
        result = juce::Result::fail ("Windows would not save the shortcut.");
    }

    link->Release();
    return result;
}

void notifyAssociationsChanged()
{
    // FLUSHNOWAIT: the cracktro keeps animating rather than waiting on Explorer.
    SHChangeNotify (SHCNE_ASSOCCHANGED, SHCNF_IDLIST | SHCNF_FLUSHNOWAIT, nullptr, nullptr);
}

} // namespace flowerinstall::shortcuts

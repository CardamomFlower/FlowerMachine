#include "WindowsGlue.h"

#include "Payload.h"

#include "InstallerConstants.h"

namespace flowerinstall
{

juce::uint32 payloadChecksum (const void* data, juce::int64 numBytes) noexcept
{
    const auto* bytes = static_cast<const juce::uint8*> (data);
    juce::uint32 sum = 2166136261u;

    for (juce::int64 i = 0; i < numBytes; ++i)
        sum = (sum ^ bytes[i]) * 16777619u;

    return sum;
}

//==============================================================================
Payload::Payload()
{
    auto* module = (HMODULE) juce::Process::getCurrentModuleInstanceHandle();

    // RT_RCDATA is MAKEINTRESOURCE(10); spelling it out avoids the ANSI/Unicode mismatch
    // that the RT_* macros carry into a UNICODE translation unit.
    auto* found = FindResourceW (module, MAKEINTRESOURCEW (1), MAKEINTRESOURCEW (10));

    if (found == nullptr)
    {
        error = "This installer was built without its payload.";
        return;
    }

    auto* handle = LoadResource (module, found);
    const auto* blob = handle != nullptr ? static_cast<const juce::uint8*> (LockResource (handle)) : nullptr;
    const auto blobSize = (juce::int64) SizeofResource (module, found);

    if (blob == nullptr || blobSize < payloadHeader::SIZE)
    {
        error = "The payload inside this installer could not be read.";
        return;
    }

    juce::MemoryInputStream header (blob, (size_t) payloadHeader::SIZE, false);

    if (header.readInt() != (int) payloadHeader::MAGIC)
    {
        error = "The payload inside this installer is not the one it expects.";
        return;
    }

    const auto declaredLength = header.readInt64();
    const auto declaredChecksum = (juce::uint32) header.readInt();

    if (declaredLength <= 0 || declaredLength > blobSize - payloadHeader::SIZE)
    {
        error = "The payload inside this installer is truncated.";
        return;
    }

    const auto* body = blob + payloadHeader::SIZE;

    if (payloadChecksum (body, declaredLength) != declaredChecksum)
    {
        error = "The payload inside this installer is damaged.";
        return;
    }

    data = body;
    length = declaredLength;
    checksum = declaredChecksum;
}

juce::Result Payload::writeTo (const juce::File& destination) const
{
    if (! isValid())
        return juce::Result::fail (error);

    if (! destination.getParentDirectory().createDirectory())
        return juce::Result::fail ("Could not create " + destination.getParentDirectory().getFullPathName());

    // Write beside the destination, then swap: a half-written executable is worse than
    // no executable. juce::File::replaceWithData does this too, but discards the reason
    // it failed, which is the one thing worth reporting here.
    auto temporary = destination.getNonexistentSibling (false);

    {
        juce::FileOutputStream out (temporary);

        if (out.failedToOpen())
            return juce::Result::fail ("Could not write to " + destination.getParentDirectory().getFullPathName()
                                       + " (" + out.getStatus().getErrorMessage().trim() + ")");

        if (! out.write (data, (size_t) length))
        {
            const auto why = out.getStatus().getErrorMessage().trim();
            temporary.deleteFile();
            return juce::Result::fail (why.isNotEmpty() ? why : juce::String ("Could not write the program file."));
        }
    }

    if (! temporary.replaceFileIn (destination))
    {
        temporary.deleteFile();

        // Overwhelmingly the reason: the program is running, so its image is locked.
        return juce::Result::fail ("Could not replace " + destination.getFileName()
                                   + ". Close FlowerMachine and run this again.");
    }

    return juce::Result::ok();
}

} // namespace flowerinstall

#pragma once

#include <JuceHeader.h>

namespace flowerinstall
{
    /*  The FlowerMachine executable, carried inside this one.

        It rides as a Win32 RCDATA resource rather than through the Projucer's
        BinaryData: a 7 MB payload turned into a C array is a ~26 MB single
        translation unit that MSVC has to chew through on every build, while the
        resource compiler simply slurps the file and the linker drops it in .rsrc.
        LockResource then hands back a pointer straight into the mapped image, so
        nothing is copied and nothing is allocated to read it.

        The blob carries its own header (magic, length, checksum) because
        SizeofResource reports the padded resource size, not the payload size.
    */
    class Payload
    {
    public:
        /** Locates the payload in this executable's own resources. */
        Payload();

        bool isValid() const noexcept { return data != nullptr && length > 0; }
        juce::int64 getLength() const noexcept { return length; }

        /** Why it could not be used; empty when isValid(). */
        const juce::String& getError() const noexcept { return error; }

        /** Writes the payload to `destination`, replacing it if it is already there.

            @param destination  the file to write
            @returns            ok, or the reason it failed
        */
        juce::Result writeTo (const juce::File& destination) const;

    private:
        const void* data = nullptr;
        juce::int64 length = 0;
        juce::uint32 checksum = 0;
        juce::String error;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Payload)
    };

    /** The header the packing script writes in front of the executable.
        Keep in step with Installer/pack-payload.py. */
    namespace payloadHeader
    {
        inline constexpr juce::uint32 MAGIC = 0x464d5031;   // "FMP1"
        inline constexpr int SIZE = 16;                     // magic, length (int64), checksum
    }

    /** Additive rolling checksum; cheap, and enough to catch a truncated resource.
        Keep in step with the packing script. */
    juce::uint32 payloadChecksum (const void* data, juce::int64 numBytes) noexcept;
}

#include "Mp3Layout.h"

#include <cstring>
#include <vector>

namespace flowermachine
{

namespace
{
    // Samples every Layer III decoder of this lineage gives out before the first real one: the
    // delay of its synthesis filter bank. Measured against JUCE's reader, not assumed.
    constexpr int decoderDelay = 529;

    // How far JUCE's reader looks past the end of a frame for the next one when the bytes there
    // are not a frame header. The count below looks the same distance, so it agrees with the
    // reader about what is junk and what is audio. The reader reads that many bytes past the
    // first four, so the furthest header it can see starts three bytes short of it.
    constexpr int resyncWindow = 32768;
    constexpr int resyncReach = resyncWindow - 3;

    // The largest Layer III frame, 320 kbit/s at 32 kHz padded: all an Info frame can take.
    constexpr int maxFrameBytes = 1441;

    struct Frame
    {
        int bytes = 0;
        int samples = 0;
        int sideInfoBytes = 0;
        int layer = 0;
        juce::uint32 kind = 0;   // layer, version, sample rate and mono or not: what a stream keeps throughout
    };

    std::optional<Frame> frameAt (const juce::uint8* h)
    {
        if (h[0] != 0xff || (h[1] & 0xe0) != 0xe0)
            return {};

        const int version = (h[1] >> 3) & 3;      // 3 MPEG-1, 2 MPEG-2, 0 MPEG-2.5, 1 reserved
        const int layer = 4 - ((h[1] >> 1) & 3);  // 1, 2 or 3; 4 is reserved
        const int bitrateIndex = h[2] >> 4;
        const int rateIndex = (h[2] >> 2) & 3;

        // A free-format stream (bitrate index 0) gives no frame size in its headers and is left
        // to the reader.
        if (version == 1 || layer == 4 || bitrateIndex == 0 || bitrateIndex == 15 || rateIndex == 3
            || (h[3] & 3) == 2)   // reserved emphasis, which the reader does not take for a header
            return {};

        static constexpr int kbps[2][3][15] =
        {
            { { 0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448 },     // MPEG-1 I
              { 0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384 },        // MPEG-1 II
              { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320 } },       // MPEG-1 III
            { { 0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256 },        // MPEG-2 I
              { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160 },             // MPEG-2 II
              { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160 } }            // MPEG-2 III
        };
        static constexpr int ratesMpeg1[] = { 44100, 48000, 32000 };
        static constexpr int ratesMpeg2[] = { 22050, 24000, 16000 };
        static constexpr int ratesMpeg25[] = { 11025, 12000, 8000 };

        const bool mpeg1 = version == 3;
        const int rate = mpeg1 ? ratesMpeg1[rateIndex] : version == 2 ? ratesMpeg2[rateIndex] : ratesMpeg25[rateIndex];
        const int bitrate = kbps[mpeg1 ? 0 : 1][layer - 1][bitrateIndex] * 1000;
        const bool mono = (h[3] >> 6) == 3;
        const int padding = (h[2] >> 1) & 1;

        Frame frame;
        frame.layer = layer;
        frame.samples = layer == 1 ? 384 : (layer == 2 || mpeg1) ? 1152 : 576;
        frame.bytes = layer == 1 ? (12 * bitrate / rate + padding) * 4
                                 : frame.samples / 8 * bitrate / rate + padding;
        frame.sideInfoBytes = mpeg1 ? (mono ? 17 : 32) : (mono ? 9 : 17);
        frame.kind = ((juce::uint32) layer << 12) | ((juce::uint32) version << 8) | ((juce::uint32) rateIndex << 4) | (mono ? 1u : 0u);
        return frame;
    }

    /** The reader's own test for a frame header, of any layer. It decodes Layer I and II as
        well, and locks on to the layer of the first header it finds. */
    bool readerTakesHeader (const juce::uint8* h)
    {
        return h[0] == 0xff && (h[1] & 0xe0) == 0xe0 && ((h[1] >> 1) & 3) != 0
            && (h[2] >> 4) != 15 && ((h[2] >> 2) & 3) != 3 && (h[3] & 3) != 2;
    }

    /** The first position at or after `from`, within the reader's reach, where a frame starts:
        of the given kind, any frame this can measure when `kind` is 0, or with `anyLayer` the first
        header of any layer, which is the one the reader locks on to. -1 when there is none. */
    juce::int64 findFrame (juce::InputStream& in, juce::int64 from, juce::uint32 kind, bool anyLayer = false)
    {
        std::vector<juce::uint8> window ((size_t) resyncWindow + 4);

        if (! in.setPosition (from))
            return -1;

        const int got = in.read (window.data(), (int) window.size());

        for (int i = 0; i + 4 <= got && i <= resyncReach; ++i)
        {
            const auto* h = window.data() + i;

            if (anyLayer ? readerTakesHeader (h)
                         : [&] { const auto frame = frameAt (h); return frame && (kind == 0 || frame->kind == kind); }())
                return from + i;

            // A free-format header of the stream's layer is one the reader takes and cannot
            // decode: its stream ends there, so the count ends there too.
            if (kind != 0 && readerTakesHeader (h) && (h[2] >> 4) == 0 && 4 - ((h[1] >> 1) & 3) == (int) (kind >> 12))
                return -1;
        }

        return -1;
    }

    struct EncoderInfo
    {
        juce::int64 frames = 0;   // audio frames, not counting the Info frame itself
        int delay = 0;
        int padding = 0;
        bool knowsGaps = false;
    };

    /** The Xing / Info frame an encoder may write first, in place of audio: a frame count, and
        in the LAME extension that follows it the encoder's delay and padding. */
    std::optional<EncoderInfo> readInfoFrame (const juce::uint8* frameStart, int available, const Frame& frame)
    {
        const int tagAt = 4 + frame.sideInfoBytes;

        if (available < tagAt + 8)
            return {};

        const auto* tag = frameStart + tagAt;

        if (std::memcmp (tag, "Xing", 4) != 0 && std::memcmp (tag, "Info", 4) != 0)
            return {};

        const auto flags = juce::ByteOrder::bigEndianInt (tag + 4);
        int offset = 8;
        EncoderInfo info;

        if ((flags & 1) != 0)
        {
            if (available < tagAt + offset + 4)
                return {};

            info.frames = juce::ByteOrder::bigEndianInt (tag + offset);
            offset += 4;
        }

        if ((flags & 2) != 0) offset += 4;     // byte count
        if ((flags & 4) != 0) offset += 100;   // seek table
        if ((flags & 8) != 0) offset += 4;     // quality

        // The LAME extension: nine bytes of encoder name, then at byte 21 the delay and the
        // padding, twelve bits each. Only the encoders known to write it are trusted.
        if (available >= tagAt + offset + 24)
        {
            const auto* lame = tag + offset;

            if (std::memcmp (lame, "LAME", 4) == 0 || std::memcmp (lame, "Lavc", 4) == 0
                || std::memcmp (lame, "Lavf", 4) == 0 || std::memcmp (lame, "GOGO", 4) == 0)
            {
                info.delay = (lame[21] << 4) | (lame[22] >> 4);
                info.padding = ((lame[22] & 0x0f) << 8) | lame[23];
                info.knowsGaps = true;
            }
        }

        return info;
    }
}

//==============================================================================
std::optional<Mp3Layout> Mp3Layout::read (const juce::File& file)
{
    juce::FileInputStream raw (file);

    if (raw.failedToOpen())
        return {};

    juce::BufferedInputStream in (raw, 1 << 16);
    const juce::int64 fileLength = raw.getTotalLength();
    juce::int64 position = 0;

    // An ID3v2 tag in front of the audio is stepped over exactly as the reader steps over it:
    // the same checks on its header, and the size that header declares after its ten bytes.
    juce::uint8 id3[10];

    if (in.read (id3, 10) == 10 && id3[0] == 'I' && id3[1] == 'D' && id3[2] == '3' && id3[4] != 0xff
        && ((id3[6] | id3[7] | id3[8] | id3[9]) & 0x80) == 0)
        position = 10 + (((juce::int64) id3[6] << 21) | ((juce::int64) id3[7] << 14)
                         | ((juce::int64) id3[8] << 7) | (juce::int64) id3[9]);

    // Where the reader locks on: the first header of any layer, since it decodes Layer I and II
    // as well. Looking for Layer III alone would find byte pairs in a Layer II payload that look
    // like a Layer III header, and make a whole file a few milliseconds long. A free-format
    // stream gives no frame sizes and is left to the reader.
    const auto firstAt = findFrame (in, position, 0, true);

    if (firstAt < 0 || ! in.setPosition (firstAt))
        return {};

    juce::uint8 frameBytes[maxFrameBytes];
    const int available = in.read (frameBytes, maxFrameBytes);

    if (available < 4)
        return {};

    const auto first = frameAt (frameBytes);

    if (! first)
        return {};

    // Only Layer III has an Info frame; Layer I and II streams are counted and kept whole.
    const auto info = first->layer == 3 ? readInfoFrame (frameBytes, juce::jmin (available, first->bytes), *first)
                                        : std::optional<EncoderInfo> {};

    // The reader looks for an Info frame only where the tag in front ends, or at the very start
    // of a file without one. Anywhere else - after a few stray bytes, say - it does not
    // recognise it and decodes it like any other frame, as a frame of silence.
    const bool readerSkipsInfo = info.has_value() && firstAt == position;
    const juce::int64 leading = info.has_value() && ! readerSkipsInfo ? first->samples : 0;

    // The frames the reader will decode, walked one by one. Always walked, even when the encoder
    // wrote a count: a file cut short by an interrupted copy has fewer frames than its count
    // says, and asking the reader for the missing ones would fail the whole load.
    juce::int64 walked = 0;
    bool backToBack = false;

    for (auto at = readerSkipsInfo ? firstAt + first->bytes : firstAt;;)
    {
        juce::uint8 header[4];

        if (! in.setPosition (at) || in.read (header, 4) != 4)
            break;

        auto frame = frameAt (header);
        const bool whereExpected = frame && frame->kind == first->kind;

        if (! whereExpected)
        {
            at = findFrame (in, at, first->kind);

            if (at < 0 || ! in.setPosition (at) || in.read (header, 4) != 4)
                break;

            frame = frameAt (header);

            if (! frame)
                break;
        }

        // A frame the file ends inside is not counted: the reader decodes what is left of it
        // into noise, at times far above full scale.
        if (at + frame->bytes > fileLength)
            break;

        if (whereExpected && walked > 0)
            backToBack = true;

        ++walked;
        at += frame->bytes;
    }

    // In a real stream each frame starts where the one before it ends. Frames that are only
    // ever found by searching are chance byte patterns in a file that is not MP3 audio at all -
    // another format given an .mp3 name, or a damaged file - and decoded they are noise that
    // can be far above full scale. Such a file is left to the reader's own estimate of its
    // length, which is far more than it can decode from a file like that, so the load fails
    // instead of playing the noise.
    if (! info && ! backToBack)
        return {};

    juce::int64 frames = walked;
    bool complete = true;

    if (info && info->frames > 0)
    {
        const juce::int64 declared = info->frames + (leading > 0 ? 1 : 0);
        complete = walked >= declared;
        frames = juce::jmin (walked, declared);
    }

    const juce::int64 total = frames * first->samples;

    if (total <= 0)
        return {};

    Mp3Layout layout;
    layout.numSamples = total;

    if (info && info->knowsGaps)
    {
        // The recording starts after the encoder's delay and the decoder's, and ends where the
        // encoder's padding begins. A file cut short has lost its padding with its last frames,
        // and keeps everything that is left. Padding shorter than the decoder's delay would
        // leave the last few samples beyond what the stream decodes to; they are given up
        // rather than asked for and not delivered.
        const juce::int64 skip = leading + info->delay + decoderDelay;
        const juce::int64 keep = complete ? juce::jmin (total - leading - info->delay - info->padding, total - skip)
                                          : total - skip;

        if (keep > 0)
        {
            layout.firstSample = skip;
            layout.numSamples = keep;
        }
    }

    return layout;
}

} // namespace flowermachine

#include "SampleLoader.h"

#include "Mp3Layout.h"
#include "Resampler.h"

namespace flowermachine
{

namespace
{
    // Frames handled between two cancellation checks. Small enough that quitting or
    // switching page is immediate, large enough to cost nothing.
    constexpr int CHUNK_FRAMES = 1 << 15;
}

//==============================================================================
class SampleLoader::Job : public juce::ThreadPoolJob
{
public:
    Job (juce::WeakReference<SampleLoader> ownerToNotify, int cartIdToLoad, int generationToReport,
         std::shared_ptr<Generations> generationsToWatch, const juce::File& fileToLoad, double rate)
        : juce::ThreadPoolJob ("Decode " + fileToLoad.getFileName()),
          owner (ownerToNotify), cartId (cartIdToLoad), generation (generationToReport),
          generations (std::move (generationsToWatch)), file (fileToLoad), targetRate (rate)
    {
    }

    JobStatus runJob() override
    {
        if (cancelled())
            return jobHasFinished;   // superseded while queued: cost is microseconds, not a whole decode

        Result result;
        result.cartId = cartId;
        result.generation = generation;
        result.sample = decode (result.error);

        if (cancelled())
            return jobHasFinished;

        juce::MessageManager::callAsync ([ownerRef = owner, result]
        {
            if (auto* loader = ownerRef.get())
                loader->deliver (result);
        });

        return jobHasFinished;
    }

private:
    /** True once this decode is pointless: the app is closing, or the cart has moved on. */
    bool cancelled() const
    {
        return shouldExit() || (*generations)[(size_t) cartId].load() != generation;
    }

    /*  Hands out the frames of the file that are its audio, in order, starting at `skip`
        frames into what the reader decodes. The frames before that are read and thrown away
        rather than jumped over: an MP3 reader seeks by counting frames of 1152 samples, which
        lands in the wrong place on a stream whose frames carry 576.
    */
    struct SequentialSource
    {
        juce::AudioFormatReader& reader;
        juce::int64 position = 0;
        bool stereo = false;

        bool skipTo (juce::int64 skip, int numChannels)
        {
            if (skip <= position)
                return true;

            juce::AudioBuffer<float> discard (numChannels, (int) juce::jmin ((juce::int64) CHUNK_FRAMES, skip - position));

            while (position < skip)
                if (! read (discard, 0, (int) juce::jmin ((juce::int64) discard.getNumSamples(), skip - position)))
                    return false;

            return true;
        }

        bool read (juce::AudioBuffer<float>& destination, int startFrame, int numFrames)
        {
            const bool ok = reader.read (&destination, startFrame, numFrames, position, true, stereo);
            position += numFrames;
            return ok;
        }
    };

    /** Reads `numFrames` frames from `source` into `destination` in chunks, checking for
        cancellation between them. */
    bool readAll (SequentialSource& source, juce::AudioBuffer<float>& destination, int numFrames) const
    {
        for (int start = 0; start < numFrames; start += CHUNK_FRAMES)
        {
            if (cancelled())
                return false;

            if (! source.read (destination, start, juce::jmin (CHUNK_FRAMES, numFrames - start)))
                return false;
        }

        return true;
    }

    SamplePtr decode (juce::String& error) const
    {
        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();

        // Read before decoding, not after: a file that changes while it is being read then
        // looks changed too, rather than being recorded as current.
        const auto modified = file.getLastModificationTime();
        const auto fileSize = file.getSize();

        const std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

        if (reader == nullptr)
        {
            error = "Unreadable or unsupported file";
            return nullptr;
        }

        if (reader->sampleRate <= 0.0 || reader->numChannels == 0)
        {
            error = "Empty file";
            return nullptr;
        }

        // Which of the decoded frames are the file's audio: all of them, except in an MP3,
        // whose encoder delay and padding are measured and left out so a loop has no gap.
        juce::int64 skipFrames = 0;
        juce::int64 sourceFrames = reader->lengthInSamples;

        if (reader->getFormatName() == juce::MP3AudioFormat().getFormatName())
        {
            if (const auto layout = Mp3Layout::read (file))
            {
                skipFrames = layout->firstSample;
                sourceFrames = layout->numSamples;
            }
        }

        if (sourceFrames <= 0)
        {
            error = "Empty file";
            return nullptr;
        }

        if (sourceFrames > (juce::int64) (reader->sampleRate * 60.0 * MAX_CART_MINUTES))
        {
            error = "Longer than " + juce::String (MAX_CART_MINUTES) + " minutes";
            return nullptr;
        }

        const int numChannels = juce::jmin (MAX_CHANNELS, (int) reader->numChannels);
        const double sourceRate = reader->sampleRate;

        auto data = std::make_shared<SampleData>();
        data->sampleRate = targetRate;
        data->sourcePath = file.getFullPathName();
        data->sourceModified = modified;
        data->sourceSize = fileSize;
        data->sourceSampleRate = sourceRate;
        data->sourceChannels = (int) reader->numChannels;
        data->durationSeconds = (double) sourceFrames / sourceRate;

        SequentialSource source { *reader, 0, numChannels > 1 };

        if (! source.skipTo (skipFrames, numChannels))
        {
            error = "Read failed";
            return nullptr;
        }

        if (juce::approximatelyEqual (sourceRate, targetRate))
        {
            data->audio.setSize (numChannels, (int) sourceFrames);

            if (! readAll (source, data->audio, (int) sourceFrames))
            {
                error = "Read failed";
                return nullptr;
            }

            data->lengthFrames = sourceFrames;
            return data;
        }

        const Resampler resampler (sourceRate, targetRate);
        const auto outFrames = resampler.outputLength (sourceFrames);

        // A file of a frame or two, converted down to a much lower rate, rounds to nothing.
        if (outFrames <= 0)
        {
            error = "Empty file";
            return nullptr;
        }

        data->audio.setSize (numChannels, (int) outFrames);

        const auto pull = [&source] (juce::AudioBuffer<float>& destination, int startFrame, int numFrames)
        {
            return source.read (destination, startFrame, numFrames);
        };

        if (! resampler.process (pull, sourceFrames, data->audio, [this] { return cancelled(); }))
        {
            if (! cancelled())
                error = "Read failed";

            return nullptr;
        }

        data->lengthFrames = data->audio.getNumSamples();
        return data;
    }

    juce::WeakReference<SampleLoader> owner;
    const int cartId;
    const int generation;
    const std::shared_ptr<Generations> generations;
    const juce::File file;
    const double targetRate;
};

//==============================================================================
SampleLoader::SampleLoader()
    : pool (juce::ThreadPoolOptions{}.withNumberOfThreads (loaderThreads())
                                      .withDesiredThreadPriority (juce::Thread::Priority::low))
{
    for (auto& generation : *generations)
        generation.store (0);
}

SampleLoader::~SampleLoader()
{
    // Every job polls its generation, so bumping them all makes running jobs abandon
    // their decode at the next chunk instead of being force-killed on the timeout.
    for (auto& generation : *generations)
        ++generation;

    pool.removeAllJobs (true, 5000);
}

void SampleLoader::request (int cartId, const juce::File& file, double targetSampleRate)
{
    if (! juce::isPositiveAndBelow (cartId, MAX_CARTS))
        return;

    const int generation = ++(*generations)[(size_t) cartId];
    pool.addJob (new Job (this, cartId, generation, generations, file, targetSampleRate), true);
}

void SampleLoader::cancel (int cartId)
{
    if (juce::isPositiveAndBelow (cartId, MAX_CARTS))
        ++(*generations)[(size_t) cartId];
}

void SampleLoader::deliver (const Result& result)
{
    if (! juce::isPositiveAndBelow (result.cartId, MAX_CARTS))
        return;

    if ((*generations)[(size_t) result.cartId].load() != result.generation)
        return;   // superseded or cancelled

    if (onResult)
        onResult (result);
}

} // namespace flowermachine

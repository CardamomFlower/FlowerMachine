#include "SampleLoader.h"

#include <cmath>

namespace flowermachine
{

namespace
{
    // Frames handled between two cancellation checks. Small enough that quitting or
    // switching page is immediate, large enough to cost nothing.
    constexpr int CHUNK_FRAMES = 1 << 15;

    // Silence after the source, so the resampler can read past the end while it flushes
    // its own latency. WindowedSinc looks 100 input samples ahead.
    constexpr int RESAMPLE_PADDING = 256;
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

    /** Reads the whole file in chunks, checking for cancellation between them. */
    bool readAll (juce::AudioFormatReader& reader, juce::AudioBuffer<float>& destination,
                  int numChannels, int numFrames) const
    {
        for (int start = 0; start < numFrames; start += CHUNK_FRAMES)
        {
            if (cancelled())
                return false;

            const int frames = juce::jmin (CHUNK_FRAMES, numFrames - start);

            if (! reader.read (&destination, start, frames, start, true, numChannels > 1))
                return false;
        }

        return true;
    }

    SamplePtr decode (juce::String& error) const
    {
        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();

        const std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

        if (reader == nullptr)
        {
            error = "Unreadable or unsupported file";
            return nullptr;
        }

        if (reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0 || reader->numChannels == 0)
        {
            error = "Empty file";
            return nullptr;
        }

        if (reader->lengthInSamples > (juce::int64) (reader->sampleRate * 60.0 * MAX_CART_MINUTES))
        {
            error = "Longer than " + juce::String (MAX_CART_MINUTES) + " minutes";
            return nullptr;
        }

        const int numChannels = juce::jmin (MAX_CHANNELS, (int) reader->numChannels);
        const int sourceFrames = (int) reader->lengthInSamples;
        const double sourceRate = reader->sampleRate;

        auto data = std::make_shared<SampleData>();
        data->sampleRate = targetRate;
        data->sourcePath = file.getFullPathName();
        data->sourceSampleRate = sourceRate;
        data->sourceChannels = (int) reader->numChannels;
        data->durationSeconds = sourceFrames / sourceRate;

        if (juce::approximatelyEqual (sourceRate, targetRate))
        {
            data->audio.setSize (numChannels, sourceFrames);

            if (! readAll (*reader, data->audio, numChannels, sourceFrames))
            {
                error = "Read failed";
                return nullptr;
            }

            data->lengthFrames = sourceFrames;
            return data;
        }

        // Offline resampling: quality over speed.
        juce::AudioBuffer<float> native (numChannels, sourceFrames + RESAMPLE_PADDING);
        native.clear();

        if (! readAll (*reader, native, numChannels, sourceFrames))
        {
            error = "Read failed";
            return nullptr;
        }

        const double ratio = sourceRate / targetRate;
        const int outFrames = juce::jmax (1, (int) std::floor (sourceFrames / ratio));

        // The interpolator's output is delayed by its algorithmic latency, and its history
        // starts zeroed: without compensation every decoded file begins with a fade-up out
        // of silence and ends early, so a loop point is a hole rather than a seam.
        const int latencyOut = (int) std::ceil (juce::WindowedSincInterpolator::getBaseLatency() / ratio);
        const int scratchFrames = outFrames + latencyOut;

        juce::AudioBuffer<float> scratch (numChannels, scratchFrames);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            juce::WindowedSincInterpolator interpolator;
            interpolator.reset();

            int inputUsed = 0;

            for (int produced = 0; produced < scratchFrames; produced += CHUNK_FRAMES)
            {
                if (cancelled())
                    return nullptr;

                const int frames = juce::jmin (CHUNK_FRAMES, scratchFrames - produced);
                inputUsed += interpolator.process (ratio,
                                                   native.getReadPointer (ch) + inputUsed,
                                                   scratch.getWritePointer (ch) + produced,
                                                   frames);
            }
        }

        data->audio.setSize (numChannels, outFrames);

        for (int ch = 0; ch < numChannels; ++ch)
            data->audio.copyFrom (ch, 0, scratch, ch, latencyOut, outFrames);

        data->lengthFrames = outFrames;
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

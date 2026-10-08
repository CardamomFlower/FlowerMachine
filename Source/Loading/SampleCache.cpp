#include "SampleCache.h"

#include "../Constants.h"

namespace flowermachine
{

namespace
{
    constexpr size_t budgetBytes = (size_t) HIDDEN_CACHE_BUDGET_MB * 1024 * 1024;
}

bool SampleCache::isWorthKeeping (const SampleData& sample)
{
    // Only what had to be resampled is slow to load again: a file already at the device's rate
    // is read and decoded in a small fraction of the time, and would only take up the budget.
    const bool resampled = ! juce::approximatelyEqual (sample.sourceSampleRate, sample.sampleRate);

    return resampled && sample.durationSeconds >= HIDDEN_CACHE_MIN_SECONDS;
}

size_t SampleCache::sizeOf (const SampleData& sample)
{
    return (size_t) sample.audio.getNumChannels() * (size_t) sample.audio.getNumSamples() * sizeof (float);
}

void SampleCache::remove (size_t index)
{
    bytesHeld -= entries[index].bytes;
    entries.erase (entries.begin() + (std::ptrdiff_t) index);
}

void SampleCache::store (const juce::File& file, SamplePtr sample)
{
    if (sample == nullptr || ! file.existsAsFile())
        return;

    const auto bytes = sizeOf (*sample);

    if (bytes > budgetBytes)
        return;

    // One entry per file and rate: two pads on the same file leave one copy, the newer.
    const auto path = file.getFullPathName();

    for (size_t i = entries.size(); i-- > 0;)
        if (entries[i].path == path && juce::approximatelyEqual (entries[i].sample->sampleRate, sample->sampleRate))
            remove (i);

    while (! entries.empty() && bytesHeld + bytes > budgetBytes)
        remove (0);

    entries.push_back ({ path, std::move (sample), bytes });
    bytesHeld += bytes;
}

SamplePtr SampleCache::take (const juce::File& file, double sampleRate)
{
    const auto path = file.getFullPathName();

    for (size_t i = 0; i < entries.size(); ++i)
    {
        auto& entry = entries[i];

        if (entry.path != path || ! juce::approximatelyEqual (entry.sample->sampleRate, sampleRate))
            continue;

        // Edited or replaced on disk since it was decoded: the audio here is no longer that file.
        // The stamp is the one taken when decoding began, not when the page was left.
        const bool unchanged = file.getLastModificationTime() == entry.sample->sourceModified
                            && file.getSize() == entry.sample->sourceSize;

        auto sample = unchanged ? std::move (entry.sample) : SamplePtr();
        remove (i);
        return sample;
    }

    return {};
}

void SampleCache::retainRate (double sampleRate)
{
    for (size_t i = entries.size(); i-- > 0;)
        if (! juce::approximatelyEqual (entries[i].sample->sampleRate, sampleRate))
            remove (i);
}

void SampleCache::clear()
{
    entries.clear();
    bytesHeld = 0;
}

} // namespace flowermachine

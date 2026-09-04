#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <functional>
#include <memory>

#include "../Constants.h"
#include "../Engine/SampleData.h"

namespace flowermachine
{
    /*  Decodes cart files on a thread pool (ARCHITECTURE.md section 5): read the
        whole file, keep the first two channels, resample to the device rate, build
        a SampleData. Results reach `onResult` on the message thread; a newer
        request for the same cart supersedes an older one, whose result is dropped.

        Cancellation is shared with the jobs, not private to the loader: a job that is
        already queued or already running checks the cart's generation as it goes and
        abandons its work, so a page the operator has moved away from does not hold the
        pool against the page they are now looking at.

        Message-thread API only. The jobs never touch the engine or the model.
    */
    class SampleLoader
    {
    public:
        struct Result
        {
            int cartId = -1;
            int generation = 0;
            SamplePtr sample;        // null on failure
            juce::String error;      // why, on failure
        };

        SampleLoader();
        ~SampleLoader();

        /** Starts decoding `file` for `cartId` at `targetSampleRate`. */
        void request (int cartId, const juce::File& file, double targetSampleRate);

        /** Abandons the cart's decode: any queued or running job for it stops as soon as
            it next looks, and its result is dropped. */
        void cancel (int cartId);

        std::function<void (const Result&)> onResult;   // message thread

    private:
        class Job;

        // Shared with every job, so a job outliving the loader is still safe to cancel.
        using Generations = std::array<std::atomic<int>, MAX_CARTS>;

        void deliver (const Result&);   // message thread

        juce::ThreadPool pool;
        std::shared_ptr<Generations> generations { std::make_shared<Generations>() };

        JUCE_DECLARE_WEAK_REFERENCEABLE (SampleLoader)
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleLoader)
    };
}

#include "Resampler.h"

#include <cmath>
#include <cstring>
#include <emmintrin.h>
#include <numeric>

namespace flowermachine
{

namespace
{
    // Flat up to here; the stopband starts at the lower of the two Nyquist frequencies.
    constexpr double passbandHz = 20000.0;

    // Attenuation from the stopband up: what is left of images and aliases.
    constexpr double attenuationDb = 102.0;

    // Fractional positions stored in the table. Between two of them the coefficients are
    // interpolated; at this count the error that leaves is below the filter's own stopband,
    // where with half as many it would sit only a few dB under the attenuation above.
    constexpr int phases = 512;

    // Input frames pulled from the source at a time; also how often cancellation is checked.
    constexpr int chunkFrames = 1 << 15;

    double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        const double quarterSquare = x * x * 0.25;

        for (int k = 1; k < 200; ++k)
        {
            term *= quarterSquare / ((double) k * (double) k);
            sum += term;

            if (term < sum * 1e-17)
                break;
        }

        return sum;
    }

    inline float horizontalSum (__m128 v)
    {
        v = _mm_add_ps (v, _mm_movehl_ps (v, v));
        v = _mm_add_ss (v, _mm_shuffle_ps (v, v, 1));
        return _mm_cvtss_f32 (v);
    }

    /*  One output frame: the input under the taps, weighted by the coefficients of the two
        stored positions either side of the exact one, and the two sums blended by `mu`.
        Written with intrinsics because the compiler, keeping to strict floating-point order,
        will not split a sum into vector lanes on its own.
    */
    inline void filterStereo (const float* h0, const float* h1, const float* left, const float* right,
                              int taps, float mu, float& outLeft, float& outRight)
    {
        __m128 l0 = _mm_setzero_ps(), l1 = _mm_setzero_ps(), r0 = _mm_setzero_ps(), r1 = _mm_setzero_ps();

        for (int k = 0; k < taps; k += 4)
        {
            const __m128 c0 = _mm_loadu_ps (h0 + k);
            const __m128 c1 = _mm_loadu_ps (h1 + k);
            const __m128 xl = _mm_loadu_ps (left + k);
            const __m128 xr = _mm_loadu_ps (right + k);

            l0 = _mm_add_ps (l0, _mm_mul_ps (c0, xl));
            l1 = _mm_add_ps (l1, _mm_mul_ps (c1, xl));
            r0 = _mm_add_ps (r0, _mm_mul_ps (c0, xr));
            r1 = _mm_add_ps (r1, _mm_mul_ps (c1, xr));
        }

        const float a0 = horizontalSum (l0), a1 = horizontalSum (l1);
        const float b0 = horizontalSum (r0), b1 = horizontalSum (r1);
        outLeft  = a0 + mu * (a1 - a0);
        outRight = b0 + mu * (b1 - b0);
    }

    inline float filterMono (const float* h0, const float* h1, const float* input, int taps, float mu)
    {
        __m128 s0 = _mm_setzero_ps(), s1 = _mm_setzero_ps();

        for (int k = 0; k < taps; k += 4)
        {
            const __m128 x = _mm_loadu_ps (input + k);
            s0 = _mm_add_ps (s0, _mm_mul_ps (_mm_loadu_ps (h0 + k), x));
            s1 = _mm_add_ps (s1, _mm_mul_ps (_mm_loadu_ps (h1 + k), x));
        }

        const float a0 = horizontalSum (s0), a1 = horizontalSum (s1);
        return a0 + mu * (a1 - a0);
    }
}

//==============================================================================
Resampler::Resampler (double sourceRate, double targetRate)
    : sourceRateHz (juce::jmax ((juce::int64) 1, (juce::int64) std::llround (sourceRate))),
      targetRateHz (juce::jmax ((juce::int64) 1, (juce::int64) std::llround (targetRate)))
{
    const double pi = juce::MathConstants<double>::pi;
    const double in = (double) sourceRateHz;

    // Passband to 20 kHz (less for a low rate), stopband from the lower Nyquist, cutoff halfway.
    const double lowNyquist = 0.5 * (double) juce::jmin (sourceRateHz, targetRateHz);
    const double passband = juce::jmin (passbandHz, lowNyquist * 0.95);
    const double cutoff = 0.5 * (passband + lowNyquist);
    const double beta = 0.1102 * (attenuationDb - 8.7);

    // Kaiser's estimate of the length for that transition band, in input samples, rounded up
    // to a whole number of vector lanes.
    const double transition = 2.0 * pi * (lowNyquist - passband) / in;
    const double estimate = (attenuationDb - 7.95) / (2.285 * transition) + 1.0;
    taps = (int) std::ceil (estimate / 8.0) * 8;
    centre = taps / 2 - 1;

    const double fc = cutoff / in;   // cycles per input sample
    const double half = 0.5 * taps;
    const double i0beta = besselI0 (beta);

    table.assign ((size_t) (phases + 1) * (size_t) taps, 0.0f);
    std::vector<double> row ((size_t) taps);

    for (int p = 0; p <= phases; ++p)
    {
        const double fraction = (double) p / (double) phases;
        double sum = 0.0;

        for (int j = 0; j < taps; ++j)
        {
            const double distance = (double) (j - centre) - fraction;
            const double u = distance / half;
            const double window = std::abs (u) <= 1.0 ? besselI0 (beta * std::sqrt (1.0 - u * u)) / i0beta : 0.0;
            const double x = 2.0 * fc * distance;
            const double sinc = std::abs (x) < 1e-12 ? 1.0 : std::sin (pi * x) / (pi * x);
            row[(size_t) j] = sinc * window;
            sum += row[(size_t) j];
        }

        // Unity gain at DC at every position, so a constant stays constant.
        for (int j = 0; j < taps; ++j)
            table[(size_t) p * (size_t) taps + (size_t) j] = (float) (row[(size_t) j] / sum);
    }
}

juce::int64 Resampler::outputLength (juce::int64 inFrames) const noexcept
{
    return (inFrames * targetRateHz + sourceRateHz / 2) / sourceRateHz;
}

//==============================================================================
void Resampler::computeRange (const float* const* window, juce::int64 windowStart, int numChannels,
                              juce::int64 inFrames, juce::int64 outFrames,
                              juce::AudioBuffer<float>& output, juce::int64 fromOutput, juce::int64 toOutput) const
{
    if (fromOutput >= toOutput)
        return;

    // Output n sits at input time n * inFrames / outFrames = whole + remainder / outFrames,
    // stepped in integers so no error builds up over a long file.
    const juce::int64 stepWhole = inFrames / outFrames;
    const juce::int64 stepRemainder = inFrames % outFrames;
    juce::int64 whole = fromOutput * inFrames / outFrames;
    juce::int64 remainder = fromOutput * inFrames % outFrames;
    const double phaseScale = (double) phases / (double) outFrames;

    float* outLeft = output.getWritePointer (0);
    float* outRight = numChannels > 1 ? output.getWritePointer (1) : nullptr;

    for (juce::int64 n = fromOutput; n < toOutput; ++n)
    {
        const double position = (double) remainder * phaseScale;
        const int phase = juce::jmin (phases - 1, (int) position);
        const float mu = (float) (position - (double) phase);
        const float* h0 = table.data() + (size_t) phase * (size_t) taps;
        const float* h1 = h0 + taps;
        const auto offset = (size_t) (whole - centre - windowStart);

        if (outRight != nullptr)
            filterStereo (h0, h1, window[0] + offset, window[1] + offset, taps, mu, outLeft[n], outRight[n]);
        else
            outLeft[n] = filterMono (h0, h1, window[0] + offset, taps, mu);

        whole += stepWhole;
        remainder += stepRemainder;

        if (remainder >= outFrames)
        {
            remainder -= outFrames;
            ++whole;
        }
    }
}

bool Resampler::process (const Source& source, juce::int64 inFrames, juce::AudioBuffer<float>& output,
                         const std::function<bool()>& cancelled) const
{
    // Near-silent input would otherwise run through the filter at the processor's slow
    // denormal speed.
    juce::ScopedNoDenormals noDenormals;

    const int numChannels = juce::jmin (2, output.getNumChannels());
    const auto outFrames = (juce::int64) output.getNumSamples();

    if (inFrames <= 0 || outFrames <= 0 || numChannels <= 0)
        return false;

    const int front = centre;          // input frames the first output reaches back over
    const int back = taps - centre;    // and the last one reaches forward over

    // The first input frame a given output's taps touch, and the outputs whose taps all lie
    // before input frame `end`.
    const auto firstTap = [this, inFrames, outFrames] (juce::int64 n) { return n * inFrames / outFrames - centre; };
    const auto outputsBefore = [this, inFrames, outFrames] (juce::int64 end)
    {
        const juce::int64 limit = end - taps + centre + 1;   // whole part must stay below this
        return limit <= 0 ? (juce::int64) 0 : juce::jmin (outFrames, (limit * outFrames - 1) / inFrames + 1);
    };

    // Kept from the two ends of the file for the wrap-around: the frames after the end are the
    // first ones again, and the frames before the start are the last ones.
    const int headFrames = (int) juce::jmin (inFrames, (juce::int64) taps);
    const int tailFrames = (int) juce::jmin (inFrames, (juce::int64) front);
    juce::AudioBuffer<float> head (numChannels, headFrames), tail (numChannels, tailFrames);

    const int capacity = front + taps + chunkFrames + back;
    juce::AudioBuffer<float> work (numChannels, capacity);
    work.clear();

    // Silence stands in for the frames before the start until the end of the file is known;
    // the outputs that saw it are worked out again at the end.
    juce::int64 windowStart = -front;
    int filled = front;
    juce::int64 taken = 0;
    juce::int64 next = 0;
    bool wrapped = false;

    while (next < outFrames)
    {
        if (cancelled())
            return false;

        if (taken < inFrames)
        {
            const int count = (int) juce::jmin ((juce::int64) chunkFrames, inFrames - taken,
                                                (juce::int64) (capacity - back - filled));

            if (count <= 0 || ! source (work, filled, count))
                return false;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float* fresh = work.getReadPointer (ch, filled);

                if (taken < headFrames)
                    head.copyFrom (ch, (int) taken, fresh, (int) juce::jmin ((juce::int64) count, headFrames - taken));

                float* kept = tail.getWritePointer (ch);

                if (count >= tailFrames)
                {
                    std::memcpy (kept, fresh + count - tailFrames, sizeof (float) * (size_t) tailFrames);
                }
                else
                {
                    std::memmove (kept, kept + count, sizeof (float) * (size_t) (tailFrames - count));
                    std::memcpy (kept + tailFrames - count, fresh, sizeof (float) * (size_t) count);
                }
            }

            filled += count;
            taken += count;
        }
        else if (! wrapped)
        {
            for (int ch = 0; ch < numChannels; ++ch)
                for (int k = 0; k < back; ++k)
                    work.setSample (ch, filled + k, head.getSample (ch, k % headFrames));

            filled += back;
            wrapped = true;
        }

        const float* window[2] = { work.getReadPointer (0), work.getReadPointer (numChannels > 1 ? 1 : 0) };
        const auto ready = outputsBefore (windowStart + filled);
        const bool progressed = ready > next;

        if (progressed)
        {
            computeRange (window, windowStart, numChannels, inFrames, outFrames, output, next, ready);
            next = ready;
        }

        if (next >= outFrames)
            break;

        // With the whole file and its wrap in the window every output is reachable, so this
        // would be a fault in the arithmetic above; stopping beats leaving the rest unwritten.
        if (wrapped && ! progressed)
            return false;

        // Nothing before the next output's first tap is needed again.
        const auto drop = (int) (firstTap (next) - windowStart);

        if (drop > 0)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* data = work.getWritePointer (ch);
                std::memmove (data, data + drop, sizeof (float) * (size_t) (filled - drop));
            }

            windowStart += drop;
            filled -= drop;
        }
    }

    // The outputs whose taps reached before the start, done again with the end of the file in
    // front of its beginning.
    const juce::int64 firstClean = juce::jmin (outFrames, (centre * outFrames + inFrames - 1) / inFrames);

    if (firstClean > 0)
    {
        juce::AudioBuffer<float> start (numChannels, front + taps);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            for (int j = -front; j < taps; ++j)
            {
                const auto index = ((j % inFrames) + inFrames) % inFrames;
                const float value = index < headFrames ? head.getSample (ch, (int) index)
                                                       : tail.getSample (ch, (int) (index - (inFrames - tailFrames)));
                start.setSample (ch, j + front, value);
            }
        }

        const float* window[2] = { start.getReadPointer (0), start.getReadPointer (numChannels > 1 ? 1 : 0) };
        computeRange (window, -front, numChannels, inFrames, outFrames, output, 0, firstClean);
    }

    return true;
}

} // namespace flowermachine

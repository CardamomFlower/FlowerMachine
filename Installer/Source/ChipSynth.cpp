#include "ChipSynth.h"

#include <cmath>

namespace flowerinstall
{

namespace
{
    constexpr float LEAD_AMP    = 0.26f;
    constexpr float HARMONY_AMP = 0.15f;
    constexpr float BASS_AMP    = 0.30f;
    constexpr float PERC_AMP    = 0.22f;

    /** PolyBLEP: rounds the corners of a square so it stops shrieking above 5 kHz. */
    inline float polyBlep (float t, float dt) noexcept
    {
        if (t < dt)
        {
            t /= dt;
            return t + t - t * t - 1.0f;
        }

        if (t > 1.0f - dt)
        {
            t = (t - 1.0f) / dt;
            return t * t + t + t + 1.0f;
        }

        return 0.0f;
    }
}

//==============================================================================
void ChipSynth::prepareToPlay (int, double sampleRate)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;

    // Everything expensive happens here, on the message thread.
    for (int note = 0; note < 128; ++note)
    {
        const double hz = 440.0 * std::pow (2.0, (note - 69) / 12.0);
        noteIncrement[(size_t) note] = (float) (hz / currentSampleRate);
    }

    samplesPerRow = juce::jmax (1, (int) (currentSampleRate * 60.0 / tune::ROWS_PER_MINUTE));
    samplesToNextRow = 0;

    // Six arpeggio steps per row: fast enough to read as a chord.
    samplesPerArpStep = juce::jmax (1, samplesPerRow / 6);
    samplesToNextArpStep = samplesPerArpStep;
    arpStep = 0;

    orderIndex = 0;
    rowIndex = 0;
    rowCounter.store (0);

    const auto voiceSettings = [this] (int index, Wave wave, float amp, double attack, double decay, float sustain)
    {
        auto& v = voices[(size_t) index];
        v = Voice();
        v.wave = wave;
        v.amplitude = amp;
        v.attack = (float) (1.0 / juce::jmax (1.0, currentSampleRate * attack));
        v.decay = (float) (1.0 / juce::jmax (1.0, currentSampleRate * decay));
        v.sustain = sustain;
    };

    voiceSettings (tune::lead,       Wave::pulse50,  LEAD_AMP,    0.004, 0.10, 0.75f);
    voiceSettings (tune::harmony,    Wave::pulse25,  HARMONY_AMP, 0.006, 0.20, 0.60f);
    voiceSettings (tune::bass,       Wave::triangle, BASS_AMP,    0.004, 0.18, 0.55f);
    voiceSettings (tune::percussion, Wave::noise,    PERC_AMP,    0.001, 0.06, 0.0f);

    // Fade the whole thing in over a quarter second, out over a fifth of one.
    masterGain = 0.0f;
    gainStep = (float) (1.0 / (currentSampleRate * 0.25));
    fadeStep = (float) (1.0 / (currentSampleRate * 0.20));
    fading.store (false);
    silent.store (false);

    // 7 kHz low-pass and 60 Hz high-pass, one pole each.
    lowPassCoeff  = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * 7000.0 / currentSampleRate));
    highPassCoeff = (float) std::exp (-juce::MathConstants<double>::twoPi * 60.0 / currentSampleRate);
    lowPassStateL = highPassStateL = highPassPrevL = 0.0f;
}

void ChipSynth::releaseResources()
{
    for (auto& v : voices)
        v.gated = false;

    masterGain = 0.0f;
    silent.store (true);
}

//==============================================================================
void ChipSynth::startNote (Voice& voice, juce::int8 note, juce::uint8 arp) noexcept
{
    if (note == tune::OFF)
    {
        voice.gated = false;
        voice.target = 0.0f;
        return;
    }

    if (note == tune::NIL)
        return;

    voice.baseNote = note;
    voice.arp = arp;
    voice.gated = true;
    voice.level = 0.0f;
    voice.target = 1.0f;

    if (voice.wave != Wave::noise)
    {
        voice.increment = noteIncrement[(size_t) juce::jlimit (0, 127, (int) note)];
        voice.phase = 0.0f;
    }
    else
    {
        // note 2 is the accent, note 1 the tap
        voice.target = note >= 2 ? 1.0f : 0.45f;
    }
}

void ChipSynth::advanceRow() noexcept
{
    const int pattern = tune::order[orderIndex];

    for (int ch = 0; ch < tune::NUM_CHANNELS; ++ch)
    {
        const auto& cell = tune::patterns[pattern][ch][rowIndex];
        startNote (voices[(size_t) ch], cell.note, cell.arp);
    }

    if (++rowIndex >= tune::PATTERN_ROWS)
    {
        rowIndex = 0;

        if (++orderIndex >= tune::ORDER_LENGTH)
            orderIndex = 0;
    }

    rowCounter.fetch_add (1);
}

float ChipSynth::renderVoice (Voice& voice, int step) noexcept
{
    // envelope: attack to 1, then decay to the sustain level; release when un-gated
    if (voice.gated)
    {
        if (voice.level < voice.target)
            voice.level = juce::jmin (voice.target, voice.level + voice.attack);
        else if (voice.level > voice.target * voice.sustain)
            voice.level = juce::jmax (voice.target * voice.sustain, voice.level - voice.decay);
    }
    else
    {
        voice.level = juce::jmax (0.0f, voice.level - voice.decay);
    }

    if (voice.level <= 0.0f)
        return 0.0f;

    if (voice.wave == Wave::noise)
    {
        // 15-bit LFSR: the classic buzz, cheaper than any random generator
        voice.noiseState = (voice.noiseState >> 1)
                         | (((voice.noiseState ^ (voice.noiseState >> 1)) & 1u) << 14);
        const float sample = (voice.noiseState & 1u) ? 1.0f : -1.0f;
        return sample * voice.level * voice.amplitude;
    }

    // arpeggio: cycle root, +a, +b every step
    float increment = voice.increment;

    if (voice.arp != 0 && step > 0)
    {
        const int offset = step == 1 ? (voice.arp >> 4) : (voice.arp & 0x0f);
        increment = noteIncrement[(size_t) juce::jlimit (0, 127, voice.baseNote + offset)];
    }

    voice.phase += increment;

    while (voice.phase >= 1.0f)
        voice.phase -= 1.0f;

    float sample = 0.0f;

    if (voice.wave == Wave::triangle)
    {
        sample = 4.0f * std::abs (voice.phase - 0.5f) - 1.0f;
    }
    else
    {
        const float width = voice.wave == Wave::pulse25 ? 0.25f : 0.5f;
        sample = voice.phase < width ? 1.0f : -1.0f;

        // two BLEPs: one at the rising edge, one at the falling one
        sample += polyBlep (voice.phase, increment);
        float shifted = voice.phase - width;
        if (shifted < 0.0f) shifted += 1.0f;
        sample -= polyBlep (shifted, increment);
    }

    return sample * voice.level * voice.amplitude;
}

void ChipSynth::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    juce::ScopedNoDenormals noDenormals;

    info.clearActiveBufferRegion();

    if (silent.load())
        return;

    const bool fadingOut = fading.load();
    const int numChannels = info.buffer->getNumChannels();
    auto* left = numChannels > 0 ? info.buffer->getWritePointer (0, info.startSample) : nullptr;
    auto* right = numChannels > 1 ? info.buffer->getWritePointer (1, info.startSample) : nullptr;

    if (left == nullptr)
        return;

    for (int i = 0; i < info.numSamples; ++i)
    {
        if (samplesToNextRow <= 0)
        {
            advanceRow();
            samplesToNextRow = samplesPerRow;
            arpStep = 0;
            samplesToNextArpStep = samplesPerArpStep;
        }

        if (--samplesToNextArpStep <= 0)
        {
            samplesToNextArpStep = samplesPerArpStep;

            if (++arpStep > 2)
                arpStep = 0;
        }

        --samplesToNextRow;

        float mix = 0.0f;

        for (auto& voice : voices)
            mix += renderVoice (voice, arpStep);

        // gentle low-pass, then a high-pass to take out the pulse DC offset
        lowPassStateL += lowPassCoeff * (mix - lowPassStateL);
        const float filtered = lowPassStateL;
        highPassStateL = highPassCoeff * (highPassStateL + filtered - highPassPrevL);
        highPassPrevL = filtered;

        masterGain = fadingOut ? juce::jmax (0.0f, masterGain - fadeStep)
                               : juce::jmin (1.0f, masterGain + gainStep);

        const float out = highPassStateL * masterGain;

        left[i] = out;

        if (right != nullptr)
            right[i] = out;

        if (fadingOut && masterGain <= 0.0f)
        {
            silent.store (true);
            return;
        }
    }
}

} // namespace flowerinstall

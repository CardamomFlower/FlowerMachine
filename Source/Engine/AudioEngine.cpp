#include "AudioEngine.h"

#include <cmath>

#include "../Constants.h"

namespace flowermachine
{

static constexpr const char* audioDeviceStateKey = "audioDeviceState";

//==============================================================================
void AudioEngine::TestTone::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    envelopeStep = (float) (1.0 / (sampleRate * DECLICK_MS * 0.001));

    active = false;
    releasing = false;
    envelope = 0.0f;
    remaining = 0;
    phase = 0.0;

    // A press made while no device was running must not fire on the next one.
    triggerRequested.store (false);
    stopRequested.store (false);
}

void AudioEngine::TestTone::render (juce::AudioBuffer<float>& output, int startSample, int numSamples)
{
    if (triggerRequested.exchange (false))
    {
        remaining = (juce::int64) (sampleRate * TEST_TONE_SECONDS);
        releasing = false;

        if (! active)
        {
            active = true;
            phase = 0.0;       // start at the zero crossing, ramp up from silence
            envelope = 0.0f;
        }
        // Already sounding: extend the burst but keep phase and envelope. Resetting them
        // mid-sine is a step to zero that no ramp covers, i.e. a click on the output the
        // operator is checking.
    }

    if (stopRequested.exchange (false) && active)
        releasing = true;

    if (! active)
        return;

    const float amplitude = juce::Decibels::decibelsToGain (TEST_TONE_DB);
    const double twoPi = juce::MathConstants<double>::twoPi;
    const double phaseIncrement = twoPi * TEST_TONE_HZ / sampleRate;
    const int numChannels = juce::jmin (output.getNumChannels(), MAX_CHANNELS);

    for (int i = 0; i < numSamples; ++i)
    {
        if (! releasing && remaining <= 0)
            releasing = true;

        envelope = releasing ? juce::jmax (0.0f, envelope - envelopeStep)
                             : juce::jmin (1.0f, envelope + envelopeStep);

        const float sample = amplitude * envelope * (float) std::sin (phase);

        phase += phaseIncrement;
        if (phase >= twoPi)
            phase -= twoPi;

        --remaining;

        for (int ch = 0; ch < numChannels; ++ch)
            output.addSample (ch, startSample + i, sample);

        if (releasing && envelope <= 0.0f)
        {
            active = false;
            break;
        }
    }
}

//==============================================================================
void AudioEngine::Source::prepareToPlay (int samplesPerBlockExpected, double newSampleRate)
{
    owner.cartEngine.prepare (newSampleRate, samplesPerBlockExpected);
    owner.tone.prepare (newSampleRate);
}

void AudioEngine::Source::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    juce::ScopedNoDenormals noDenormals;

    info.clearActiveBufferRegion();
    owner.cartEngine.render (*info.buffer, info.startSample, info.numSamples);
    owner.tone.render (*info.buffer, info.startSample, info.numSamples);
}

//==============================================================================
AudioEngine::AudioEngine (juce::PropertiesFile& settingsToUse)
    : settings (settingsToUse)
{
    const std::unique_ptr<juce::XmlElement> savedState (settings.getXmlValue (audioDeviceStateKey));
    initError = deviceManager.initialise (0, MAX_CHANNELS, savedState.get(), true);
    deviceManager.addChangeListener (this);

    player.setSource (&source);
    deviceManager.addAudioCallback (&player);
}

AudioEngine::~AudioEngine()
{
    deviceManager.removeAudioCallback (&player);
    player.setSource (nullptr);
    deviceManager.removeChangeListener (this);
}

void AudioEngine::triggerTestTone()
{
    tone.triggerRequested.store (true);
}

void AudioEngine::stopAll()
{
    tone.stopRequested.store (true);
    cartEngine.push ({ Command::Type::stopAll, 0 });
}

juce::String AudioEngine::getDeviceDescription() const
{
    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
        return initError.isNotEmpty() ? "No output device: " + initError
                                      : juce::String ("No output device");

    juce::String s;
    s << device->getTypeName() << " - " << device->getName()
      << " - " << juce::String (device->getCurrentSampleRate() / 1000.0, 1) << " kHz"
      << " - " << device->getCurrentBufferSizeSamples() << " samples";
    return s;
}

void AudioEngine::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (const auto xml = deviceManager.createStateXml())
        settings.setValue (audioDeviceStateKey, xml.get());
    else
        settings.removeValue (audioDeviceStateKey);

    if (onDeviceChanged)
        onDeviceChanged();
}

} // namespace flowermachine

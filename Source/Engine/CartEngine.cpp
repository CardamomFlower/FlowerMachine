#include "CartEngine.h"

namespace flowermachine
{

//==============================================================================
// Message thread

void CartEngine::publish (int cartId, SamplePtr sample)
{
    if (! isValidCart (cartId))
        return;

    auto& cart = carts[(size_t) cartId];
    cart.length.store (sample != nullptr ? sample->lengthFrames : 0);
    cart.slot.publish (std::move (sample));
}

void CartEngine::unload (int cartId)
{
    if (! isValidCart (cartId))
        return;

    auto& cart = carts[(size_t) cartId];
    cart.length.store (0);
    cart.slot.unload();   // publish (nullptr) alone would free nothing - see SampleSlot.h
}

void CartEngine::pruneAll()
{
    for (auto& cart : carts)
        cart.slot.prune();
}

bool CartEngine::push (const Command& command)
{
    return commands.push (command);
}

void CartEngine::setGainDb (int cartId, float gainDb)
{
    if (isValidCart (cartId))
        carts[(size_t) cartId].gain.store (
            juce::Decibels::decibelsToGain (juce::jlimit (CART_GAIN_DB_MIN, CART_GAIN_DB_MAX, gainDb)));
}

void CartEngine::setLoop (int cartId, bool shouldLoop)
{
    if (isValidCart (cartId))
        carts[(size_t) cartId].loop.store (shouldLoop);
}

//==============================================================================
// UI reads

bool CartEngine::isPlaying (int cartId) const noexcept
{
    return isValidCart (cartId) && carts[(size_t) cartId].playing.load();
}

juce::uint32 CartEngine::getStartCount (int cartId) const noexcept
{
    return isValidCart (cartId) ? carts[(size_t) cartId].startCount.load() : 0;
}

juce::int64 CartEngine::getPlayheadFrames (int cartId) const noexcept
{
    return isValidCart (cartId) ? carts[(size_t) cartId].playhead.load() : 0;
}

juce::int64 CartEngine::getLengthFrames (int cartId) const noexcept
{
    return isValidCart (cartId) ? carts[(size_t) cartId].length.load() : 0;
}

//==============================================================================
// Audio thread

void CartEngine::prepare (double newSampleRate, int)
{
    const double rate = newSampleRate > 0.0 ? newSampleRate : 44100.0;

    sampleRate.store (rate);
    envelopeStep  = (float) (1.0 / (rate * DECLICK_MS * 0.001));
    releaseFrames = (juce::int64) (rate * DECLICK_MS * 0.001);

    for (auto& voice : voices)
        voice.gain.reset (rate, 0.02);

    reset();
}

void CartEngine::reset()
{
    // Nothing drains the FIFO while no device runs, so clicks made before this device
    // opened would otherwise all fire in the first block. Draining is the consumer's
    // job and prepare()/reset() stand in for the callback, so this is race-free.
    Command discard;
    while (commands.pop (discard)) {}

    for (auto& voice : voices)
        retire (voice);

    voiceCount.fill (0);

    for (auto& cart : carts)
        cart.playing.store (false);
}

void CartEngine::render (juce::AudioBuffer<float>& output, int startSample, int numSamples)
{
    Command command;

    for (int handled = 0; handled < COMMAND_FIFO_SIZE && commands.pop (command); ++handled)
        handle (command);

    for (auto& voice : voices)
        if (voice.isActive())
            renderVoice (voice, output, startSample, numSamples);
}

void CartEngine::handle (const Command& command)
{
    switch (command.type)
    {
        case Command::Type::play:
            if (isValidCart (command.cartId))
            {
                releaseOthersNotLooping (command.cartId);   // D2 revised: a new cart replaces the playing one, beds stay
                releaseVoicesOf (command.cartId);           // restart: the old instance fades over DECLICK_MS
                startVoice (command.cartId, command.ignoreLoop);
            }
            break;

        case Command::Type::stop:
            releaseVoicesOf (command.cartId);
            break;

        case Command::Type::stopAll:
            releaseAll();
            break;

        case Command::Type::flushAll:
            for (auto& voice : voices)
                retire (voice);
            break;
    }
}

void CartEngine::startVoice (int cartId, bool ignoreLoop)
{
    auto& cart = carts[(size_t) cartId];
    auto sample = cart.slot.acquire();

    if (sample == nullptr || sample->lengthFrames <= 0)
        return;   // nothing loaded; the controller normally prevents this

    for (auto& voice : voices)
    {
        if (voice.isActive())
            continue;

        voice.sample = std::move (sample);
        voice.cartId = cartId;
        voice.position = 0;
        voice.envelope = 0.0f;
        voice.releaseStep = envelopeStep;
        voice.ignoreLoop = ignoreLoop;
        voice.phase = Voice::Phase::attack;
        voice.gain.setCurrentAndTargetValue (cart.gain.load());

        if (voiceCount[(size_t) cartId]++ == 0)
            cart.playing.store (true);

        cart.startCount.fetch_add (1);
        cart.playhead.store (0);
        return;
    }

    // No free voice: the click is ignored (64 simultaneous carts is a full page).
    // `sample` is released here; the slot still holds a reference, so this is never the last one.
}

void CartEngine::beginRelease (Voice& voice, float step)
{
    voice.phase = Voice::Phase::release;
    voice.releaseStep = step;
}

void CartEngine::releaseVoicesOf (int cartId)
{
    for (auto& voice : voices)
        if (voice.isActive() && voice.cartId == cartId)
            beginRelease (voice, envelopeStep);
}

void CartEngine::releaseOthersNotLooping (int cartId)
{
    for (auto& voice : voices)
    {
        if (! voice.isActive() || voice.cartId == cartId)
            continue;

        // What spares a voice is looping, not the cart's flag: a voice started inside a
        // sequence ignores that flag, so it is a one-shot and must give way like any other.
        if (carts[(size_t) voice.cartId].loop.load() && ! voice.ignoreLoop)
            continue;

        beginRelease (voice, envelopeStep);
    }
}

void CartEngine::releaseAll()
{
    for (auto& voice : voices)
        if (voice.isActive())
            beginRelease (voice, envelopeStep);
}

void CartEngine::retire (Voice& voice)
{
    if (voice.isActive() && isValidCart (voice.cartId))
    {
        auto& count = voiceCount[(size_t) voice.cartId];

        if (--count <= 0)
        {
            count = 0;
            carts[(size_t) voice.cartId].playing.store (false);
        }
    }

    voice.phase = Voice::Phase::idle;
    voice.sample.reset();   // never the last reference: the slot ring or retire list still holds one
    voice.cartId = -1;
}

void CartEngine::renderVoice (Voice& voice, juce::AudioBuffer<float>& output, int startSample, int numSamples)
{
    auto& cart = carts[(size_t) voice.cartId];
    const auto& sample = *voice.sample;
    const auto length = sample.lengthFrames;
    const bool loop = cart.loop.load() && ! voice.ignoreLoop;

    voice.gain.setTargetValue (cart.gain.load());

    const int numOut = juce::jmin (output.getNumChannels(), MAX_CHANNELS);
    const float* in0 = sample.audio.getReadPointer (0);
    const float* in1 = sample.audio.getNumChannels() > 1 ? sample.audio.getReadPointer (1) : in0;
    float* out0 = numOut > 0 ? output.getWritePointer (0, startSample) : nullptr;
    float* out1 = numOut > 1 ? output.getWritePointer (1, startSample) : nullptr;

    // A single-channel output gets the sum, not just the left channel: dropping the right
    // half of a stereo jingle is silence for anything panned right. (For a mono sample
    // in1 == in0, so the sum is the original signal.)
    const bool sumToMono = numOut == 1 && sample.audio.getNumChannels() > 1;

    for (int i = 0; i < numSamples; ++i)
    {
        if (voice.position >= length)
        {
            if (! loop)
            {
                retire (voice);
                return;
            }

            voice.position = 0;   // seamless wrap
        }

        // A non-looping file fades over its last DECLICK_MS. Loop can be switched off
        // inside that window, leaving fewer frames than the ramp needs, so the step is
        // sized to what is actually left: the envelope always reaches 0 at the file end.
        if (! loop && voice.phase != Voice::Phase::release && voice.position >= length - releaseFrames)
            beginRelease (voice, juce::jmax (envelopeStep,
                                             voice.envelope / (float) juce::jmax ((juce::int64) 1, length - voice.position)));

        switch (voice.phase)
        {
            case Voice::Phase::attack:
                voice.envelope += envelopeStep;

                if (voice.envelope >= 1.0f)
                {
                    voice.envelope = 1.0f;
                    voice.phase = Voice::Phase::sustain;
                }
                break;

            case Voice::Phase::release:
                voice.envelope -= voice.releaseStep;

                if (voice.envelope <= 0.0f)
                {
                    retire (voice);
                    return;
                }
                break;

            case Voice::Phase::sustain:
            case Voice::Phase::idle:
                break;
        }

        const float g = voice.envelope * voice.gain.getNextValue();
        const auto pos = (int) voice.position;

        if (out0 != nullptr) out0[i] += (sumToMono ? 0.5f * (in0[pos] + in1[pos]) : in0[pos]) * g;
        if (out1 != nullptr) out1[i] += in1[pos] * g;

        ++voice.position;
    }

    cart.playhead.store (voice.position);
}

} // namespace flowermachine

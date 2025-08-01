#include "Engine.h"

namespace engine {

Engine::Engine()
    : voicePool(*this){
    parameters[PARAM_VOLUME].setValue(1.0f, true);
    parameters[PARAM_EXPRESSION].setValue(1.0f, true);
    parameters[PARAM_VIBRATO].setValue(0.0f, true);
}

void Engine::prepareToPlay(float sr, int samplesPerBlock)
{
    externalSampleRate = sr;

    interpolator.setRatio(INTERNAL_SAMPLE_RATE / externalSampleRate);
    interpolator.reset();
    remainedSamples = 0;

    voicePool.prepareToPlay(INTERNAL_SAMPLE_RATE, SUB_FRAME_LENGTH);

    keysState.reset();
    sustained = false;
}

void Engine::process(float* outL, float* outR, size_t numFrames)
{
    float* origOutL{ outL };
    float* origOutR{ outR };
    size_t origNumFrames{ numFrames };

    while (numFrames > 0) {
        if (remainedSamples > 0) {
            const size_t idx = SUB_FRAME_LENGTH - remainedSamples;
            const float* subL{ subFrameBuffer.getReadPointer(0, idx) };
            const float* subR{ subFrameBuffer.getReadPointer(1, idx) };

            while (remainedSamples > 0 && interpolator.canWrite()) {
                interpolator.write(*subL, *subR);
                --remainedSamples;
                subL += 1;
                subR += 1;
            }

            while (numFrames > 0 && interpolator.canRead()) {
                interpolator.read(*outL, *outR);
                numFrames -= 1;
                outL += 1;
                outR += 1;
            }
        }

        if (remainedSamples == 0 && numFrames > 0) {
            processSubFrame();
            jassert(remainedSamples > 0);
        }
    }
}

void Engine::processMidiMessage(const MidiMessage& msg)
{
    if (msg.isNoteOn())
        noteOn(msg);
    else if (msg.isNoteOff())
        noteOff(msg);
    else if (msg.isController())
        controlChange(msg);
}

void Engine::processLyrics()
{
    Lyrics::Ptr lyricsPtr{};
    Lyrics::Ptr ptr{};

    do {
        if (lyricsPtr != nullptr)
            disposeLyricsQueue.send(lyricsPtr);

        lyricsPtr = ptr;
    } while (setLyricsQueue.receive(ptr));

    if (lyricsPtr == nullptr)
        return;

    jassert(lyricsPtr == ptr);

    size_t numPhrases{ jmin(lyricsPtr->size(), lyricsMaxPhrases) };

    for (size_t i = 0; i < numPhrases; ++i) {
        const auto& phrase{ lyricsPtr->operator[](i) };

        lyrics[i].parse(phrase.attack, phrase.release);
    }

    lyricsNumPhrases = numPhrases;
    phraseIndex = 0;

    disposeLyricsQueue.send(lyricsPtr);
}

Result Engine::setLyrics(const String& str)
{
    auto lyricsPtr{ std::make_shared<Lyrics>() };
    auto res{ lyricsPtr->parse(str) };

    if (res.wasOk()) {
        return setLyrics(lyricsPtr);
    }

    return res;
}

void Engine::rewind()
{
    phraseIndex = 0;
}

Result Engine::setLyrics(const Lyrics::Ptr& ptr)
{
    if (setLyricsQueue.send(ptr)) {
        cachedLyrics = ptr;
        return Result::ok();
    }

    return Result::fail("Queue is full");
}

const Lyrics::Phrase& Engine::getCurrentPhrase() const
{
    const static Lyrics::Phrase dummy{};

    if (cachedLyrics != nullptr) {
        size_t idx{ getCurrentPhraseIndex() };

        if (idx == 0)
            idx = cachedLyrics->size();

        --idx;

        if (idx < cachedLyrics->size()) {
            return cachedLyrics->operator[](idx);
        }
    }

    return dummy;
}

void Engine::performHousekeeping()
{
    Lyrics::Ptr ptr{};

    while (disposeLyricsQueue.receive(ptr))
        ptr.reset();
}

void Engine::updateParameters(size_t numFrames)
{
    // Here we update parameters that do not get per-sample integration,
    // so they get updates once per block instead.

    parameters[PARAM_VIBRATO].getNextValue(numFrames);
}

void Engine::noteOn(const MidiMessage& msg)
{
    keysState.set(msg.getNoteNumber());

    if (phraseIndex >= lyricsNumPhrases) {
        // No lyrics available
        return;
    }

    const float velocity{ (float)msg.getVelocity() / 127.0f };

    Voice::Trigger trigger{};
    trigger.key = msg.getNoteNumber();
    trigger.velocity = velocity;
    trigger.envelope.sampleRate = INTERNAL_SAMPLE_RATE;
    trigger.envelope.attack = envelopeAttack * (3.0f - 2.0f * velocity * parameters[PARAM_EXPRESSION].getTargetValue());
    trigger.envelope.decay = envelopeDecay;
    trigger.envelope.sustain = envelopeSustain;
    trigger.envelope.release = envelopeRelease;

    trigger.phrase = lyrics[phraseIndex];
    phraseIndex = (phraseIndex + 1) % lyricsNumPhrases;

    bool triggered{ false };

    if (legato) {
        if (auto* voice{ activeVoices.first() }) {
            voice->retrigger(trigger);
            triggered = true;
        }
    }

    if (!triggered) {
        if (auto* voice{ voicePool.trigger(trigger) }) {
            activeVoices.append(voice);
        }
    }
}

void Engine::noteOff(const MidiMessage& msg)
{
    keysState.reset(msg.getNoteNumber());

    if (sustained)
        return;

    auto* voice{ activeVoices.first() };

    while (voice != nullptr) {
        if (voice->getTriggerRecord().key == msg.getNoteNumber())
            voice->release();

        voice = voice->next();
    }
}

void Engine::controlChange(const MidiMessage& msg)
{
    constexpr int CC_SUSTAIN = 64;

    if (msg.getControllerNumber() == CC_SUSTAIN) {
        bool wasSustained{ sustained };
        sustained = msg.getControllerValue() > 63;

        if (wasSustained && (!sustained)) {
            releaseSustainedVoices();
        }
    }
}

void Engine::releaseSustainedVoices()
{
    auto* voice{ activeVoices.first() };

    while (voice != nullptr) {
        if (!keysState[voice->getTriggerRecord().key])
            voice->release();

        voice = voice->next();
    }
}

void Engine::processSubFrame()
{
    updateParameters(SUB_FRAME_LENGTH);

    subFrameBuffer.clear();
    mixBuffer.clear();

    float* outL{ subFrameBuffer.getWritePointer(0) };
    float* outR{ subFrameBuffer.getWritePointer(1) };

    auto* voice{ activeVoices.first() };

    while (voice != nullptr) {
        float* mixL{ mixBuffer.getWritePointer(0) };
        float* mixR{ mixBuffer.getWritePointer(1) };
        voice->process(mixL, mixR, SUB_FRAME_LENGTH);

        for (size_t i = 0; i < SUB_FRAME_LENGTH; ++i) {
            outL[i] += mixL[i];
            outR[i] += mixR[i];
        }

        if (voice->isOver()) {
            auto* nextVoice{ activeVoices.removeAndReturnNext(voice) };
            voicePool.recycle(voice);
            voice = nextVoice;
        } else {
            voice = voice->next();
        }
    }

    // Apply volume and expression
    for (size_t i = 0; i < SUB_FRAME_LENGTH; ++i) {
        const float volume{ parameters[PARAM_VOLUME].getNextValue() };
        const float expression{ parameters[Engine::PARAM_EXPRESSION].getNextValue() };
        const float gain{ volume * (0.1f + expression * expression) / 1.1f };

        outL[i] *= gain;
        outR[i] *= gain;
    }

    remainedSamples = SUB_FRAME_LENGTH;
}

} // namespace engine

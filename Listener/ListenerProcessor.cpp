#include "ListenerProcessor.h"
#include "ListenerEditor.h"
#include "../SharedMemory.h"

ListenerProcessor::ListenerProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    // Claim a free index — this keeps the handle open for the lifetime
    listenerIndex = SharedMemory::claimFreeIndex (dataRegion);

    if (listenerIndex >= 0)
    {
        auto* audio = SharedMemory::getAudio (dataRegion);
        if (audio != nullptr)
        {
            audio->numSamplesWritten.store (0);
            // isConnected is already set to 1 by claimFreeIndex
            DBG ("[MorphListener] Claimed index: " + juce::String (listenerIndex));
        }
        else
        {
            DBG ("[MorphListener] FAILED to get audio data for index " + juce::String (listenerIndex));
            listenerIndex = -1;
        }
    }
    else
    {
        DBG ("[MorphListener] FAILED to claim any index!");
    }
}

ListenerProcessor::~ListenerProcessor()
{
    if (dataRegion.ptr != nullptr)
    {
        auto* audio = SharedMemory::getAudio (dataRegion);
        if (audio != nullptr)
        {
            audio->isConnected.store (0);
            audio->numSamplesWritten.store (0);
        }
    }
    SharedMemory::destroy (dataRegion);
    dataRegion = { SharedMemory::InvalidHandle, nullptr, 0, {} };
    listenerIndex = -1;
}

void ListenerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    if (auto* audio = SharedMemory::getAudio (dataRegion))
    {
        audio->sampleRate = sampleRate;
        audio->numSamplesWritten.store (0);
    }
}

void ListenerProcessor::releaseResources() {}

void ListenerProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                       juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    processBlockCallCount.fetch_add (1, std::memory_order_relaxed);

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples == 0)
        return;

    // Write to shared memory for the Morph plugin
    if (auto* audioData = SharedMemory::getAudio (dataRegion))
    {
        if (audioData->isConnected.load() == 0)
            return;

        const int toWrite = juce::jmin (numSamples, (int) ListenerAudioData::maxSamples);

        // Seqlock write: increment seq to odd (locked), write data,
        // increment seq to even (unlocked).
        uint32_t seq = audioData->seq.load (std::memory_order_relaxed);
        audioData->seq.store (seq + 1, std::memory_order_release);

        audioData->numSamplesWritten.store (toWrite, std::memory_order_relaxed);

        if (numChannels > 0)
            juce::FloatVectorOperations::copy (audioData->bufferL, buffer.getReadPointer (0), toWrite);
        else
            juce::FloatVectorOperations::clear (audioData->bufferL, toWrite);

        if (numChannels > 1)
            juce::FloatVectorOperations::copy (audioData->bufferR, buffer.getReadPointer (1), toWrite);
        else
            juce::FloatVectorOperations::copy (audioData->bufferR, audioData->bufferL, toWrite);

        // Defensively zero-fill any portion beyond what we wrote.
        if (toWrite < (int) ListenerAudioData::maxSamples)
        {
            juce::FloatVectorOperations::clear (audioData->bufferL + toWrite,
                                                  (int) ListenerAudioData::maxSamples - toWrite);
            juce::FloatVectorOperations::clear (audioData->bufferR + toWrite,
                                                  (int) ListenerAudioData::maxSamples - toWrite);
        }

        auto* ph = getPlayHead();
        if (ph != nullptr)
        {
            auto posInfo = ph->getPosition();
            if (posInfo.hasValue())
                audioData->hostTimeInSamples = (int64_t) posInfo->getTimeInSamples().orFallback (0);
        }

        // Unlock: seq becomes even again.  Morph can now safely read.
        audioData->seq.store (seq + 2, std::memory_order_release);
        audioData->processBlockCount.fetch_add (1, std::memory_order_release);
    }
}

bool ListenerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainInputChannelSet().isDisabled())
        return false;
    if (layouts.getMainOutputChannelSet().isDisabled())
        return false;

    int mainIn = layouts.getMainInputChannelSet().size();
    if (mainIn < 1 || mainIn > 2)
        return false;

    int mainOut = layouts.getMainOutputChannelSet().size();
    if (mainOut < 1 || mainOut > 2)
        return false;

    return true;
}

juce::AudioProcessorEditor* ListenerProcessor::createEditor()
{
    return new ListenerEditor (*this);
}

void ListenerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    (void) destData;
}

void ListenerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    (void) data;
    (void) sizeInBytes;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ListenerProcessor();
}
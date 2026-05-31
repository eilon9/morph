#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../SharedMemory.h"
#include "FFTMorphEngine.cpp"

MorphProcessor::MorphProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), false)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    for (int i = 0; i < numSlots; ++i)
    {
        connections[i].listenerIndex = -1;
    }
}

MorphProcessor::~MorphProcessor()
{
    for (int i = 0; i < numSlots; ++i)
        disconnectSlot (i);
}

juce::AudioProcessorValueTreeState::ParameterLayout MorphProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "blend", "Blend",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));

    juce::StringArray choices;
    choices.add ("None");
    for (int i = 1; i <= MaxListeners; ++i)
        choices.add ("Listener " + juce::String (i));

    for (int i = 0; i < numSlots; ++i)
    {
        int defaultIdx = (i < 2) ? (i + 1) : 0;
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            "ch" + juce::String (i),
            "Slot " + juce::String (i + 1),
            choices,
            defaultIdx));
    }

    return layout;
}

void MorphProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize  = samplesPerBlock;
    ensureConnections();
    fftMorph.reset();
    scratchBEverPopulated = false;
}

void MorphProcessor::releaseResources()
{
    for (int i = 0; i < numSlots; ++i)
        disconnectSlot (i);
}

void MorphProcessor::ensureConnections()
{
    for (int i = 0; i < numSlots; ++i)
    {
        if (auto* p = apvts.getRawParameterValue ("ch" + juce::String (i)))
        {
            int choice = juce::roundToInt (p->load());
            int listenerIdx = choice - 1; // 0 = "None"

            // Already connected — verify still alive before skipping
            if (connections[i].listenerIndex == listenerIdx &&
                connections[i].dataRegion.ptr != nullptr)
            {
                auto* existing = SharedMemory::getAudio (connections[i].dataRegion);
                if (existing != nullptr && existing->isConnected.load() != 0)
                    continue;
                // Listener died or restarted — drop and reconnect below
                disconnectSlot (i);
            }

            // Disconnect if wrong listener
            if (connections[i].listenerIndex != listenerIdx)
                disconnectSlot (i);

            // Connect if not "None"
            if (listenerIdx >= 0 && listenerIdx < MaxListeners)
            {
                auto region = SharedMemory::openIndex (listenerIdx);
                if (region.ptr == nullptr)
                    continue;

                auto* audio = SharedMemory::getAudio (region);
                if (audio == nullptr || audio->isConnected.load() == 0)
                {
                    SharedMemory::destroy (region);
                    continue;
                }

                connections[i].dataRegion = region;
                connections[i].listenerIndex = listenerIdx;
                DBG ("[Morph] Slot " + juce::String (i + 1)
                     + " connected to Listener " + juce::String (listenerIdx + 1));
            }
        }
    }
}

void MorphProcessor::disconnectSlot (int slot)
{
    if (slot < 0 || slot >= numSlots)
        return;
    SharedMemory::destroy (connections[slot].dataRegion);
    connections[slot].listenerIndex = -1;
}

bool MorphProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet().isDisabled())
        return false;

    int mainOut = layouts.getMainOutputChannelSet().size();
    if (mainOut < 1 || mainOut > 2)
        return false;

    // Input bus is optional and ignored (audio comes from shared memory).
    // Accept disabled or any 1-2 channel layout.
    const auto& in = layouts.getMainInputChannelSet();
    if (!in.isDisabled() && (in.size() < 1 || in.size() > 2))
        return false;

    return true;
}

void MorphProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                    juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Lazy reconnection
    ensureConnections();

    const int numSamples = buffer.getNumSamples();
    const int outCh = juce::jmin (buffer.getNumChannels(), 2);

    if (numSamples <= 0)
        return;

    // Read blend (0..1)
    float blend = 0.0f;
    if (auto* bp = apvts.getRawParameterValue ("blend"))
        blend = juce::jlimit (0.0f, 1.0f, bp->load());

    // Map blend 0→1 across all 8 slots, same as BPMSampler morphPos 0→7.
    const float pos   = blend * (float)(numSlots - 1);
    const int   slotA = juce::jlimit (0, numSlots - 2, (int) std::floor (pos));
    const int   slotB = slotA + 1;
    const float frac  = pos - (float) slotA;

    static thread_local juce::AudioBuffer<float> scratchA (2, ListenerAudioData::maxSamples);
    static thread_local juce::AudioBuffer<float> scratchB (2, ListenerAudioData::maxSamples);

    // Per-thread flags: has this thread's scratch buffer ever held a clean read?
    // Used to fall back to stale-but-valid audio instead of silence on seqlock
    // contention (happens in Reaper when unrouted tracks process in parallel).
    static thread_local bool scratchAClean = false;
    static thread_local bool scratchBClean = false;

    // Reads listener audio into scratch.
    // Returns true  → scratch has usable data (fresh or stale from last good read).
    // Returns false → listener is fully disconnected or has never written; output silence.
    //
    // On seqlock contention (Reaper parallel-track race): scratch is left unchanged
    // and we return true so the previous frame is repeated rather than silenced.
    // We read into a temp buffer first so a partial write never corrupts scratch.
    auto readFromListener = [] (ListenerConnection& conn,
                                 juce::AudioBuffer<float>& scratch,
                                 bool& scratchEverClean,
                                 int numSamples) -> bool
    {
        if (conn.listenerIndex < 0 || conn.dataRegion.ptr == nullptr)
            return false;

        auto* audio = SharedMemory::getAudio (conn.dataRegion);
        if (audio == nullptr || audio->isConnected.load() == 0)
            return false;

        // Listener exists but hasn't completed a single write yet.
        if (audio->processBlockCount.load (std::memory_order_acquire) == 0)
            return false;

        const uint32_t seq0 = audio->seq.load (std::memory_order_acquire);
        if ((seq0 & 1) != 0)  // odd → Listener is mid-write right now
            return scratchEverClean;  // repeat last frame instead of silencing

        // Read into a temp buffer so scratch is never left half-overwritten
        // if the seqlock check fails after we start copying.
        static thread_local std::array<float, ListenerAudioData::maxSamples> tempL, tempR;

        const int samplesWritten = audio->numSamplesWritten.load (std::memory_order_relaxed);
        const int valid          = juce::jlimit (0, (int) ListenerAudioData::maxSamples, samplesWritten);
        const int toRead         = juce::jmin (numSamples, valid);

        if (toRead > 0)
        {
            juce::FloatVectorOperations::copy (tempL.data(), audio->bufferL, toRead);
            juce::FloatVectorOperations::copy (tempR.data(), audio->bufferR, toRead);
        }

        // Verify seqlock — a write completed while we were copying.
        const uint32_t seq1 = audio->seq.load (std::memory_order_acquire);
        if (seq0 != seq1)
            return scratchEverClean;  // repeat last frame instead of silencing

        // Clean read: commit temp → scratch.
        if (toRead > 0)
        {
            juce::FloatVectorOperations::copy (scratch.getWritePointer (0), tempL.data(), toRead);
            juce::FloatVectorOperations::copy (scratch.getWritePointer (1), tempR.data(), toRead);
        }
        if (toRead < numSamples)
        {
            juce::FloatVectorOperations::clear (scratch.getWritePointer (0) + toRead,
                                                  numSamples - toRead);
            juce::FloatVectorOperations::clear (scratch.getWritePointer (1) + toRead,
                                                  numSamples - toRead);
        }

        scratchEverClean = true;
        return true;
    };

    const bool hasA = readFromListener (connections[slotA], scratchA, scratchAClean, numSamples);
    const bool hasB = readFromListener (connections[slotB], scratchB, scratchBClean, numSamples);
    if (scratchBClean) scratchBEverPopulated = true;

    // No source A at all → silence
    if (!hasA)
    {
        for (int ch = 0; ch < outCh; ++ch)
            juce::FloatVectorOperations::clear (buffer.getWritePointer (ch), numSamples);
        return;
    }

    // B not yet available — either not configured, or hasn't written its first frame yet.
    // If B was never populated, fall back to A pass-through (nothing to morph toward).
    if (!scratchBEverPopulated)
    {
        const float gain = 1.0f - frac;
        for (int ch = 0; ch < outCh; ++ch)
            buffer.copyFrom (ch, 0, scratchA.getReadPointer (ch), numSamples, gain);
        return;
    }

    // FFT spectral morph — works at any block size via ring buffer accumulation
    fftMorph.process (scratchA, scratchB, buffer, frac, numSamples);
}

bool MorphProcessor::isSlotConnected (int slotIndex) const noexcept
{
    if (slotIndex < 0 || slotIndex >= numSlots)
        return false;
    if (connections[slotIndex].listenerIndex < 0 || connections[slotIndex].dataRegion.ptr == nullptr)
        return false;
    auto* audio = SharedMemory::getAudio (connections[slotIndex].dataRegion);
    return audio != nullptr && audio->isConnected.load() != 0;
}

float MorphProcessor::getSlotLevel (int slotIndex) const noexcept
{
    if (slotIndex < 0 || slotIndex >= numSlots)
        return 0.0f;
    if (connections[slotIndex].listenerIndex < 0)
        return 0.0f;

    auto* audio = SharedMemory::getAudio (connections[slotIndex].dataRegion);
    if (audio == nullptr || audio->isConnected.load() == 0)
        return 0.0f;

    // Seqlock read: ensure we get a consistent snapshot
    const uint32_t seq0 = audio->seq.load (std::memory_order_acquire);
    if ((seq0 & 1) != 0)  // odd → being written
        return 0.0f;

    const int written = audio->numSamplesWritten.load (std::memory_order_relaxed);
    const int meterSamples = juce::jmin (256, written, (int) ListenerAudioData::maxSamples);
    float sumL = 0.0f, sumR = 0.0f;

    for (int i = 0; i < meterSamples; ++i)
    {
        sumL += std::abs (audio->bufferL[i]);
        sumR += std::abs (audio->bufferR[i]);
    }

    // Verify seqlock
    if (audio->seq.load (std::memory_order_acquire) != seq0)
        return 0.0f;

    float avg = (meterSamples > 0) ? (sumL + sumR) / (2.0f * meterSamples) : 0.0f;
    return juce::jlimit (0.0f, 1.0f, avg * 3.0f);
}

juce::AudioProcessorEditor* MorphProcessor::createEditor()
{
    return new MorphEditor (*this);
}

void MorphProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (xml != nullptr)
        copyXmlToBinary (*xml, destData);
}

void MorphProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MorphProcessor();
}
#pragma once
#include <JuceHeader.h>
#include "../SharedMemory.h"
#include "FFTMorphEngine.h"

class MorphProcessor : public juce::AudioProcessor
{
public:
    MorphProcessor();
    ~MorphProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Morph"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    bool supportsDoublePrecisionProcessing() const override { return false; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    static constexpr int numSlots = 8;
    float getSlotLevel (int slotIndex) const noexcept;
    bool  isSlotConnected (int slotIndex) const noexcept;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    struct ListenerConnection
    {
        SharedMemory::MappedRegion dataRegion;
        int listenerIndex = -1;
    };

    ListenerConnection connections[numSlots];
    void ensureConnections();
    void disconnectSlot (int slot);

    double currentSampleRate = 44100.0;
    int    currentBlockSize  = 512;

    // FFT morph engine
    FFTMorphEngine fftMorph;
    bool scratchBEverPopulated = false; // true once B listener has sent at least one frame

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MorphProcessor)
};

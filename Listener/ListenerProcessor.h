#pragma once
#include <JuceHeader.h>
#include "../SharedMemory.h"

class ListenerProcessor : public juce::AudioProcessor
{
public:
    ListenerProcessor();
    ~ListenerProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "MorphListener"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    bool supportsDoublePrecisionProcessing() const override { return false; }

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    int getListenerIndex()    const noexcept { return listenerIndex; }
    int getProcessBlockCount() const noexcept { return processBlockCallCount.load (std::memory_order_relaxed); }

private:
    int listenerIndex = -1;
    SharedMemory::MappedRegion dataRegion;
    std::atomic<int> processBlockCallCount { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ListenerProcessor)
};
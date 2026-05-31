#pragma once
#include <JuceHeader.h>

class ListenerProcessor;

class ListenerEditor : public juce::AudioProcessorEditor,
                       private juce::Timer
{
public:
    explicit ListenerEditor (ListenerProcessor&);
    ~ListenerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    ListenerProcessor& listenerProcessor;

    juce::Label indexLabel;
    juce::Label statusLabel;
    int lastProcessBlockCount = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ListenerEditor)
};
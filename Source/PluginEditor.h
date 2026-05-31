#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class MorphEditor : public juce::AudioProcessorEditor,
                    private juce::Timer
{
public:
    explicit MorphEditor (MorphProcessor&);
    ~MorphEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    MorphProcessor& morphProcessor;

    static constexpr int numSlots = 8;

    juce::Label    slotLabels[numSlots];
    juce::ComboBox slotBoxes[numSlots];
    juce::Label    slotStatus[numSlots];  // live connection indicator
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>, numSlots> slotAttachments;

    juce::Slider blendSlider;
    juce::Label  blendLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> blendAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MorphEditor)
};

#include "PluginEditor.h"

static const juce::Colour BG      { 0xff1a1a2e };
static const juce::Colour ACCENT  { 0xff7c7cff };
static const juce::Colour DIMTEXT { 0xff888899 };

MorphEditor::MorphEditor (MorphProcessor& p)
    : AudioProcessorEditor (&p), morphProcessor (p)
{
    setSize (560, 420);

    juce::StringArray choices;
    choices.add ("None");
    for (int i = 1; i <= 16; ++i)
        choices.add ("Listener " + juce::String (i));

    for (int i = 0; i < numSlots; ++i)
    {
        slotLabels[i].setText ("Slot " + juce::String (i + 1), juce::dontSendNotification);
        slotLabels[i].setJustificationType (juce::Justification::centredLeft);
        slotLabels[i].setColour (juce::Label::textColourId, DIMTEXT);
        addAndMakeVisible (slotLabels[i]);

        slotBoxes[i].addItemList (choices, 1);
        addAndMakeVisible (slotBoxes[i]);

        slotStatus[i].setJustificationType (juce::Justification::centred);
        slotStatus[i].setFont (juce::Font (juce::FontOptions (11.0f)));
        slotStatus[i].setText ("--", juce::dontSendNotification);
        slotStatus[i].setColour (juce::Label::textColourId, DIMTEXT);
        addAndMakeVisible (slotStatus[i]);

        slotAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            morphProcessor.apvts,
            "ch" + juce::String (i),
            slotBoxes[i]);
    }

    // Blend slider
    blendSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    blendSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 24);
    blendSlider.setRange (0.0, 1.0, 0.001);
    addAndMakeVisible (blendSlider);

    blendAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        morphProcessor.apvts, "blend", blendSlider);

    blendLabel.setText ("Morph blend", juce::dontSendNotification);
    blendLabel.setJustificationType (juce::Justification::centredLeft);
    blendLabel.setColour (juce::Label::textColourId, DIMTEXT);
    addAndMakeVisible (blendLabel);

    startTimerHz (4);
}

MorphEditor::~MorphEditor()
{
    stopTimer();
}

void MorphEditor::timerCallback()
{
    static const juce::Colour GREEN  { 0xff44dd44 };
    static const juce::Colour ORANGE { 0xffddaa44 };

    for (int i = 0; i < numSlots; ++i)
    {
        float level = morphProcessor.getSlotLevel (i);
        bool connected = morphProcessor.isSlotConnected (i);

        if (!connected)
        {
            slotStatus[i].setText ("--", juce::dontSendNotification);
            slotStatus[i].setColour (juce::Label::textColourId, DIMTEXT);
        }
        else if (level > 0.01f)
        {
            slotStatus[i].setText ("live", juce::dontSendNotification);
            slotStatus[i].setColour (juce::Label::textColourId, GREEN);
        }
        else
        {
            slotStatus[i].setText ("conn", juce::dontSendNotification);
            slotStatus[i].setColour (juce::Label::textColourId, ORANGE);
        }
    }
}

void MorphEditor::paint (juce::Graphics& g)
{
    g.fillAll (BG);

    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (20.0f)).boldened());
    g.drawText ("MORPH", getLocalBounds().removeFromTop (46), juce::Justification::centred);

    g.setColour (ACCENT.withAlpha (0.3f));
    g.drawHorizontalLine (47, 18.0f, (float) getWidth() - 18.0f);
}

void MorphEditor::resized()
{
    auto area = getLocalBounds().reduced (18);
    area.removeFromTop (46);

    auto selectorArea = area.removeFromTop (8 * 34);

    for (int i = 0; i < numSlots; ++i)
    {
        auto row = selectorArea.removeFromTop (34).reduced (4, 2);
        slotLabels[i].setBounds (row.removeFromLeft (72));
        slotBoxes[i].setBounds (row.removeFromLeft (280));
        row.removeFromLeft (8);
        slotStatus[i].setBounds (row.removeFromLeft (48));
    }

    area.removeFromTop (16);
    blendLabel.setBounds (area.removeFromTop (24));
    blendSlider.setBounds (area.removeFromTop (40).reduced (0, 4));
}
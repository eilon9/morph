#include "ListenerEditor.h"
#include "ListenerProcessor.h"
#include "../SharedMemory.h"

static const juce::Colour BG       { 0xff1a1a2e };
static const juce::Colour ACCENT   { 0xff7c7cff };
static const juce::Colour DIMTEXT  { 0xff888899 };
static const juce::Colour GREEN    { 0xff44dd44 };

ListenerEditor::ListenerEditor (ListenerProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setSize (240, 120);

    indexLabel.setText ("Listener #" + juce::String (processor.getListenerIndex() + 1),
                        juce::dontSendNotification);
    indexLabel.setFont (juce::Font (juce::FontOptions (28.0f)).boldened());
    indexLabel.setJustificationType (juce::Justification::centred);
    indexLabel.setColour (juce::Label::textColourId, ACCENT);
    addAndMakeVisible (indexLabel);

    statusLabel.setFont (juce::Font (juce::FontOptions (14.0f)));
    statusLabel.setJustificationType (juce::Justification::centred);
    statusLabel.setColour (juce::Label::textColourId, DIMTEXT);
    addAndMakeVisible (statusLabel);

    startTimerHz (4);
}

ListenerEditor::~ListenerEditor()
{
    stopTimer();
}

void ListenerEditor::timerCallback()
{
    int idx = processor.getListenerIndex();

    if (idx < 0)
    {
        statusLabel.setText ("No index assigned!", juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, juce::Colours::red);
        return;
    }

    int count = processor.getProcessBlockCount();
    if (count != lastProcessBlockCount)
    {
        lastProcessBlockCount = count;
        statusLabel.setText ("processBlock running (" + juce::String (count) + ")",
                             juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, GREEN);
    }
    else
    {
        statusLabel.setText ("processBlock NOT called! (" + juce::String (count) + ")",
                             juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, juce::Colours::red);
    }
}

void ListenerEditor::paint (juce::Graphics& g)
{
    g.fillAll (BG);

    g.setColour (processor.getListenerIndex() >= 0 ? ACCENT : juce::Colours::red);
    g.drawRect (getLocalBounds(), 2);

    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText ("MORPH LISTENER",
                getLocalBounds().removeFromTop (18),
                juce::Justification::centred);
}

void ListenerEditor::resized()
{
    auto area = getLocalBounds().reduced (10);
    area.removeFromTop (18);

    indexLabel.setBounds (area.removeFromTop (40));
    statusLabel.setBounds (area.removeFromTop (30));
}
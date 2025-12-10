/*
  DDX3216 Cathedral Reverb Plugin - Editor Header
  JUCE 8.0.11
*/

#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class DdxReverbAudioProcessorEditor : public juce::AudioProcessorEditor,
    private juce::Timer
{
public:
    DdxReverbAudioProcessorEditor(DdxReverbAudioProcessor&);
    ~DdxReverbAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    DdxReverbAudioProcessor& audioProcessor;

    // Sliders with labels
    struct ControlGroup
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    ControlGroup decayControl;
    ControlGroup predelayControl;
    ControlGroup dampingControl;
    ControlGroup diffusionControl;
    ControlGroup hicutControl;
    ControlGroup bassmultControl;
    ControlGroup wetControl;

    juce::ToggleButton bypassButton;
    juce::ToggleButton simdButton;
    juce::Label processingModeLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> simdAttachment;

    // CPU meter
    float currentCpuUsage = 0.0f;

    void setupControl(ControlGroup& control, const juce::String& paramID, const juce::String& labelText);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DdxReverbAudioProcessorEditor)
};
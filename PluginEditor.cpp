/*
  DDX3216 Cathedral Reverb Plugin - Editor Implementation
  JUCE 8.0.11
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
DdxReverbAudioProcessorEditor::DdxReverbAudioProcessorEditor(DdxReverbAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setSize(700, 380);

    // Setup controls
    setupControl(decayControl, "decay", "Decay Time");
    setupControl(predelayControl, "predelay", "Pre-Delay");
    setupControl(dampingControl, "damping", "Damping");
    setupControl(diffusionControl, "diffusion", "Diffusion");
    setupControl(hicutControl, "hicut", "Hi Cut");
    setupControl(bassmultControl, "bassmult", "Bass Mult");
    setupControl(wetControl, "wet", "Wet/Dry");

    // Bypass button
    addAndMakeVisible(bypassButton);
    bypassButton.setButtonText("Bypass");
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "bypass", bypassButton);

    // SIMD mode toggle
    addAndMakeVisible(simdButton);
    simdButton.setButtonText("Use SIMD (Low CPU)");
    simdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "simd", simdButton);

    // Processing mode label
    addAndMakeVisible(processingModeLabel);
    processingModeLabel.setText("Processing Mode:", juce::dontSendNotification);
    processingModeLabel.setJustificationType(juce::Justification::centredLeft);
    processingModeLabel.setFont(juce::FontOptions(14.0f, juce::Font::bold)); // Fixed: JUCE 8 FontOptions

    // Start timer for CPU monitoring
    startTimerHz(10);
}

DdxReverbAudioProcessorEditor::~DdxReverbAudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
void DdxReverbAudioProcessorEditor::setupControl(ControlGroup& control,
    const juce::String& paramID,
    const juce::String& labelText)
{
    addAndMakeVisible(control.slider);
    control.slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    control.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);

    addAndMakeVisible(control.label);
    control.label.setText(labelText, juce::dontSendNotification);
    control.label.setJustificationType(juce::Justification::centred);
    control.label.attachToComponent(&control.slider, false);

    control.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), paramID, control.slider);
}

//==============================================================================
void DdxReverbAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Background gradient (DDX3216 blue/grey theme)
    g.fillAll(juce::Colour(0xff2a2d3a));

    auto bounds = getLocalBounds();

    // Header
    auto headerArea = bounds.removeFromTop(60);
    g.setGradientFill(juce::ColourGradient(
        juce::Colour(0xff3a4a5a), 0.0f, 0.0f,
        juce::Colour(0xff2a3a4a), 0.0f, static_cast<float>(headerArea.getHeight()), false));
    g.fillRect(headerArea);

    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(28.0f, juce::Font::bold)); // Fixed: FontOptions
    g.drawFittedText("DDX3216 CATHEDRAL REVERB", headerArea.reduced(10, 0),
        juce::Justification::centred, 1);

    g.setFont(juce::FontOptions(12.0f)); // Fixed: FontOptions
    g.drawFittedText("SHARC DSP Authentic Port",
        headerArea.reduced(10, 35).removeFromBottom(15),
        juce::Justification::centred, 1);

    // Control panel background
    auto controlArea = bounds.removeFromTop(220);
    g.setColour(juce::Colour(0xff1a1d2a));
    g.fillRoundedRectangle(controlArea.reduced(10, 5).toFloat(), 8.0f);

    // Footer with CPU meter
    auto footerArea = bounds;
    g.setColour(juce::Colour(0xff1a1d2a));
    g.fillRect(footerArea.reduced(10, 5));

    // CPU usage meter
    bool usingSIMD = *audioProcessor.getAPVTS().getRawParameterValue("simd") > 0.5f;
    juce::String cpuText = juce::String("CPU: ") + juce::String(currentCpuUsage * 100.0f, 1) + "% | Mode: "
        + (usingSIMD ? "SIMD (Optimized)" : "Scalar (Authentic)");

    g.setColour(usingSIMD ? juce::Colours::lightgreen : juce::Colours::orange);
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold)); // Fixed: FontOptions
    g.drawText(cpuText, footerArea.reduced(15, 10), juce::Justification::centredLeft);

    // Draw dividers
    g.setColour(juce::Colour(0xff4a5a6a).withAlpha(0.3f));
    g.drawLine(10.0f, 60.0f, static_cast<float>(getWidth()) - 10.0f, 60.0f, 1.0f);
    g.drawLine(10.0f, static_cast<float>(getHeight()) - 95.0f, static_cast<float>(getWidth()) - 10.0f,
        static_cast<float>(getHeight()) - 95.0f, 1.0f);
}

//==============================================================================
void DdxReverbAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop(65); // Skip header

    // Control sliders in a row
    auto controlArea = bounds.removeFromTop(200).reduced(20, 10);
    const int sliderWidth = 80;
    const int spacing = 5;

    decayControl.slider.setBounds(controlArea.removeFromLeft(sliderWidth));
    controlArea.removeFromLeft(spacing);

    predelayControl.slider.setBounds(controlArea.removeFromLeft(sliderWidth));
    controlArea.removeFromLeft(spacing);

    dampingControl.slider.setBounds(controlArea.removeFromLeft(sliderWidth));
    controlArea.removeFromLeft(spacing);

    diffusionControl.slider.setBounds(controlArea.removeFromLeft(sliderWidth));
    controlArea.removeFromLeft(spacing);

    hicutControl.slider.setBounds(controlArea.removeFromLeft(sliderWidth));
    controlArea.removeFromLeft(spacing);

    bassmultControl.slider.setBounds(controlArea.removeFromLeft(sliderWidth));
    controlArea.removeFromLeft(spacing * 3);

    // Wet/dry slider (taller)
    wetControl.slider.setBounds(controlArea.removeFromLeft(sliderWidth));

    // Footer controls
    auto footerArea = bounds.removeFromTop(80).reduced(20, 10);

    processingModeLabel.setBounds(footerArea.removeFromTop(25));

    auto buttonArea = footerArea.removeFromTop(30);
    bypassButton.setBounds(buttonArea.removeFromLeft(120));
    buttonArea.removeFromLeft(20);
    simdButton.setBounds(buttonArea.removeFromLeft(200));
}

//==============================================================================
void DdxReverbAudioProcessorEditor::timerCallback()
{
    // Update CPU usage display
    currentCpuUsage = audioProcessor.getCpuUsage();
    repaint(0, getHeight() - 95, getWidth(), 95); // Only repaint footer
}
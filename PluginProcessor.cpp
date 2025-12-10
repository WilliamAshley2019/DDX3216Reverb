/*
  DDX3216 Cathedral Reverb Plugin - Implementation
  JUCE 8.0.11
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

// Prime-number delays (scaled for 48kHz, typical in SHARC reverbs)
const int DdxReverbAudioProcessor::combDelays[numCombs] = { 1116, 1188, 1277, 1356 };
const int DdxReverbAudioProcessor::allpassDelays[numAllpasses] = { 556, 441, 313, 391, 347, 113, 37, 59 };

//==============================================================================
DdxReverbAudioProcessor::DdxReverbAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "PARAMS", createParameterLayout())
{
}

DdxReverbAudioProcessor::~DdxReverbAudioProcessor()
{
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout DdxReverbAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // DDX3216 Cathedral parameters (based on SysEx spec)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "decay", "Decay Time",
        juce::NormalisableRange<float>(2.0f, 20.0f, 0.1f), 5.0f, "s"));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "predelay", "Pre-Delay",
        juce::NormalisableRange<float>(0.0f, 500.0f, 1.0f), 50.0f, "ms"));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "damping", "Damping (Hi Decay)",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 50.0f, "%"));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "diffusion", "Diffusion",
        juce::NormalisableRange<float>(0.0f, 20.0f, 0.1f), 10.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "hicut", "Hi Shelf Cut",
        juce::NormalisableRange<float>(0.0f, 30.0f, 0.1f), 0.0f, "dB"));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "bassmult", "Bass Multiply",
        juce::NormalisableRange<float>(-10.0f, 10.0f, 0.1f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "wet", "Wet/Dry Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "bypass", "Bypass", false));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "simd", "Use SIMD (Low CPU)", false));

    return { params.begin(), params.end() };
}

//==============================================================================
void DdxReverbAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    // Allocate buffers
    dryBuffer.setSize(2, samplesPerBlock);
    tempBuffer.setSize(1, samplesPerBlock);

    // Pre-delay buffer (max 500ms)
    int maxPreDelay = static_cast<int>(sampleRate * 0.5);
    preDelayBuffer.resize(static_cast<size_t>(maxPreDelay));
    std::fill(preDelayBuffer.begin(), preDelayBuffer.end(), 0.0f);
    preDelayWritePos = 0;

    // Prepare comb filters
    int maxCombDelay = static_cast<int>(sampleRate * 0.1); // 100ms max
    for (int i = 0; i < numCombs; ++i)
    {
        combs[i].prepare(sampleRate, maxCombDelay, 0.7f, 5000.0f);

        // Scale delays to current sample rate
        int scaledDelay = static_cast<int>(combDelays[i] * sampleRate / 48000.0);
        combs[i].setDelaySamples(scaledDelay);
    }

    // Prepare all-pass filters
    int maxAPDelay = static_cast<int>(sampleRate * 0.05); // 50ms max
    for (int i = 0; i < numAllpasses; ++i)
    {
        allpasses[i].prepare(sampleRate, maxAPDelay, 0.5f);

        // Scale delays to current sample rate
        int scaledDelay = static_cast<int>(allpassDelays[i] * sampleRate / 48000.0);
        allpasses[i].setDelaySamples(scaledDelay);
    }
}

void DdxReverbAudioProcessor::releaseResources()
{
}

bool DdxReverbAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

//==============================================================================
void DdxReverbAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // CPU monitoring
    auto startTime = juce::Time::getMillisecondCounterHiRes();

    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    auto numSamples = buffer.getNumSamples();

    // Clear unused output channels
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, numSamples);

    // Bypass
    if (*apvts.getRawParameterValue("bypass") > 0.5f)
        return;

    // Get parameters
    float decayTime = *apvts.getRawParameterValue("decay");
    float predelayMs = *apvts.getRawParameterValue("predelay");
    float dampingPct = *apvts.getRawParameterValue("damping");
    float diffusion = *apvts.getRawParameterValue("diffusion");
    float hiCutDb = *apvts.getRawParameterValue("hicut");
    float bassMult = *apvts.getRawParameterValue("bassmult");
    float wetMix = *apvts.getRawParameterValue("wet");
    useSIMD = *apvts.getRawParameterValue("simd") > 0.5f;

    // Store dry signal
    dryBuffer.makeCopyOf(buffer, true);

    // Convert to mono (sum L+R)
    auto* monoData = tempBuffer.getWritePointer(0);
    juce::FloatVectorOperations::copy(monoData, buffer.getReadPointer(0), numSamples);

    if (totalNumInputChannels > 1)
    {
        juce::FloatVectorOperations::add(monoData, buffer.getReadPointer(1), numSamples);
        juce::FloatVectorOperations::multiply(monoData, 0.5f, numSamples);
    }

    // Apply hi-cut filter (input lowpass)
    if (hiCutDb > 0.01f)
    {
        float cutoffGain = juce::Decibels::decibelsToGain(-hiCutDb);
        static float prevSample = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            prevSample = prevSample + cutoffGain * (monoData[i] - prevSample);
            monoData[i] = prevSample;
        }
    }

    // Pre-delay - CORRECTED: Read old sample FIRST, then write new
    preDelaySamples = static_cast<int>(predelayMs * currentSampleRate / 1000.0f);
    preDelaySamples = juce::jlimit(0, static_cast<int>(preDelayBuffer.size()) - 1, preDelaySamples);

    if (preDelaySamples > 0)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            // Calculate read position (looking back in time)
            int readPos = preDelayWritePos - preDelaySamples;
            if (readPos < 0)
                readPos += static_cast<int>(preDelayBuffer.size());

            // 1. READ old delayed sample
            float delayed = preDelayBuffer[static_cast<size_t>(readPos)];

            // 2. WRITE current input to buffer
            preDelayBuffer[static_cast<size_t>(preDelayWritePos)] = monoData[i];

            // 3. OUTPUT the delayed sample
            monoData[i] = delayed;

            // 4. Advance write position
            if (++preDelayWritePos >= static_cast<int>(preDelayBuffer.size()))
                preDelayWritePos = 0;
        }
    }

    // Update comb filter parameters
    // Damping: 0% = bright (20kHz), 100% = dark (2kHz)
    float dampingFreq = juce::jmap(dampingPct, 0.0f, 100.0f, 20000.0f, 2000.0f);

    // Decay time affects feedback gain: RT60 = -60dB decay time
    // g = 10^(-3 * T / RT60) where T is delay time in seconds
    float avgDelayMs = (combDelays[0] + combDelays[1] + combDelays[2] + combDelays[3]) / 4.0f
        * 1000.0f / static_cast<float>(currentSampleRate);
    float combGain = std::pow(10.0f, -3.0f * avgDelayMs / (decayTime * 1000.0f));
    combGain = juce::jlimit(0.1f, 0.99f, combGain);

    // Bass multiply boosts/cuts low-frequency decay
    combGain *= (1.0f + bassMult * 0.05f);

    for (auto& comb : combs)
    {
        comb.setDampingFreq(dampingFreq);
        comb.setGain(combGain);
    }

    // Process parallel combs - accumulate in monoData
    juce::AudioBuffer<float> combBuffer(1, numSamples);

    for (int c = 0; c < numCombs; ++c)
    {
        auto* combOut = combBuffer.getWritePointer(0);
        combBuffer.clear();

        if (useSIMD)
            combs[c].processBlockSIMD(monoData, combOut, numSamples);
        else
            combs[c].processBlockScalar(monoData, combOut, numSamples);

        // Mix combs equally (parallel topology)
        if (c == 0)
            juce::FloatVectorOperations::copy(monoData, combOut, numSamples);
        else
            juce::FloatVectorOperations::add(monoData, combOut, numSamples);
    }

    // Scale down after parallel sum
    juce::FloatVectorOperations::multiply(monoData, 0.25f, numSamples);

    // Process series all-passes for diffusion
    // Diffusion: 0 = minimal, 20 = maximum
    float apGain = juce::jmap(diffusion, 0.0f, 20.0f, 0.3f, 0.7f);

    for (auto& ap : allpasses)
    {
        ap.setGain(apGain);

        if (useSIMD)
            ap.processBlockSIMD(monoData, monoData, numSamples);
        else
            ap.processBlockScalar(monoData, monoData, numSamples);
    }

    // Mix wet/dry (output to stereo with phase inversion for width)
    for (int channel = 0; channel < totalNumOutputChannels; ++channel)
    {
        auto* outData = buffer.getWritePointer(channel);
        auto* dryData = dryBuffer.getReadPointer(juce::jmin(channel, dryBuffer.getNumChannels() - 1));

        // Dry signal (1 - wet)
        juce::FloatVectorOperations::copy(outData, dryData, numSamples);
        juce::FloatVectorOperations::multiply(outData, 1.0f - wetMix, numSamples);

        // Add wet signal - STEREO WIDTH: invert right channel phase
        if (channel == 1)
        {
            // Right channel: invert phase for stereo width (DDX3216 style)
            juce::FloatVectorOperations::addWithMultiply(outData, monoData, -wetMix, numSamples);
        }
        else
        {
            // Left channel: normal polarity
            juce::FloatVectorOperations::addWithMultiply(outData, monoData, wetMix, numSamples);
        }
    }

    // Update CPU usage
    auto endTime = juce::Time::getMillisecondCounterHiRes();
    double blockTime = (endTime - startTime) / 1000.0; // seconds
    double expectedBlockTime = static_cast<double>(numSamples) / currentSampleRate;
    cpuUsage = blockTime / expectedBlockTime;
}

//==============================================================================
juce::AudioProcessorEditor* DdxReverbAudioProcessor::createEditor()
{
    return new DdxReverbAudioProcessorEditor(*this);
}

//==============================================================================
void DdxReverbAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void DdxReverbAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState != nullptr)
        if (xmlState->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DdxReverbAudioProcessor();
}
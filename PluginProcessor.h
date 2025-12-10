/*
  DDX3216 Cathedral Reverb Plugin
  JUCE 8.0.11 - Faithful SHARC DSP Port with SIMD Optimization

  Based on Behringer DDX3216's SHARC ADSP-21160 reverb algorithms
  Supports both scalar (authentic) and SIMD (optimized) processing
*/

#pragma once
#include <JuceHeader.h>

//==============================================================================
// SHARC-style Feedback Comb Filter (Classic Schroeder Topology)
// This is the CORRECT implementation used in vintage digital reverbs
//==============================================================================
class SharcCombFilter
{
public:
    SharcCombFilter() = default;

    void prepare(double sRate, int maxDelaySamples, float initialGain = 0.7f, float dampingFreq = 5000.0f)
    {
        delayLine.resize(maxDelaySamples);
        std::fill(delayLine.begin(), delayLine.end(), 0.0f);
        writeIndex = 0;
        this->delaySamples = juce::jlimit(1, maxDelaySamples, maxDelaySamples);
        this->feedbackGain = initialGain;
        this->sRate = sRate;

        // Damping filter (one-pole lowpass in feedback path)
        dampingCoeff = std::exp(-juce::MathConstants<float>::twoPi * dampingFreq / (float)sRate);
        filterState = 0.0f;
        prepared = true;
    }

    void setDelaySamples(int newDelay)
    {
        delaySamples = juce::jlimit(1, (int)delayLine.size(), newDelay);
    }

    void setGain(float newGain)
    {
        feedbackGain = juce::jlimit(0.0f, 0.99f, newGain);
    }

    void setDampingFreq(float freq)
    {
        dampingCoeff = std::exp(-juce::MathConstants<float>::twoPi * freq / (float)sRate);
    }

    void reset()
    {
        std::fill(delayLine.begin(), delayLine.end(), 0.0f);
        writeIndex = 0;
        filterState = 0.0f;
    }

    // Scalar version - CORRECT feedback comb topology
    // Read old delayed sample FIRST, then write new sample
    void processBlockScalar(const float* input, float* output, int numSamples) noexcept
    {
        if (!prepared) return;

        auto* buffer = delayLine.data();
        int idx = writeIndex;
        const int len = delaySamples;
        const float g = feedbackGain;
        const float damp = dampingCoeff;
        float flt = filterState;

        for (int i = 0; i < numSamples; ++i)
        {
            // 1. READ old delayed sample
            float delayed = buffer[idx];

            // 2. Apply one-pole lowpass damping to feedback
            flt = delayed + damp * (flt - delayed);

            // 3. FEEDBACK comb: new sample = input + g * dampedFeedback
            float newSample = input[i] + g * flt;

            // 4. WRITE new sample to buffer
            buffer[idx] = newSample;

            // 5. OUTPUT is the delayed sample (or mix with input)
            output[i] = newSample;

            // 6. Advance circular buffer
            if (++idx >= len) idx = 0;
        }

        writeIndex = idx;
        filterState = flt;
    }

    // SIMD version - same algorithm, vectorized
    void processBlockSIMD(const float* input, float* output, int numSamples) noexcept
    {
        if (!prepared) return;

        using SIMD = juce::dsp::SIMDRegister<float>;
        constexpr size_t simdWidth = SIMD::SIMDRegister::size();

        auto* buffer = delayLine.data();
        int idx = writeIndex;
        const int len = delaySamples;
        const float g = feedbackGain;
        const float damp = dampingCoeff;
        float flt = filterState;

        size_t vectorSamples = (static_cast<size_t>(numSamples) / simdWidth) * simdWidth;

        // SIMD main loop
        for (size_t i = 0; i < vectorSamples; i += simdWidth)
        {
            alignas(32) float delayed[8] = { 0 }; // Max SIMD width
            for (size_t j = 0; j < simdWidth; ++j)
                delayed[j] = buffer[(idx + static_cast<int>(j)) % len];

            SIMD delayedVec = SIMD::fromRawArray(delayed);
            SIMD inputVec = SIMD::fromRawArray(input + i);

            // Apply damping (simplified - per-sample would be more accurate)
            alignas(32) float dampedVals[8] = { 0 };
            for (size_t j = 0; j < simdWidth; ++j)
            {
                flt = delayed[j] + damp * (flt - delayed[j]);
                dampedVals[j] = flt;
            }

            SIMD dampedVec = SIMD::fromRawArray(dampedVals);
            SIMD gVec(g);
            SIMD outVec = inputVec + gVec * dampedVec; // Fixed: proper SIMD multiplication

            outVec.copyToRawArray(output + i);

            // Store back to buffer
            alignas(32) float toStore[8] = { 0 };
            outVec.copyToRawArray(toStore);
            for (size_t j = 0; j < simdWidth; ++j)
                buffer[(idx + static_cast<int>(j)) % len] = toStore[j];

            idx = (idx + static_cast<int>(simdWidth)) % len;
        }

        // Scalar tail
        for (int i = static_cast<int>(vectorSamples); i < numSamples; ++i)
        {
            float delayed = buffer[idx];
            flt = delayed + damp * (flt - delayed);
            float newSample = input[i] + g * flt;
            buffer[idx] = newSample;
            output[i] = newSample;
            if (++idx >= len) idx = 0;
        }

        writeIndex = idx;
        filterState = flt;
    }

private:
    std::vector<float> delayLine;
    int delaySamples = 1000;
    int writeIndex = 0;
    float feedbackGain = 0.7f;
    float dampingCoeff = 0.5f;
    float filterState = 0.0f;
    double sRate = 48000.0;
    bool prepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SharcCombFilter)
};

//==============================================================================
// SHARC-style All-Pass Filter (Classic Schroeder topology)
// Formula: y[n] = -g*x[n] + x[n-M] + g*y[n-M]
//==============================================================================
class SharcAllpassFilter
{
public:
    SharcAllpassFilter() = default;

    void prepare(double /*sampleRate*/, int maxDelaySamples, float initialGain = 0.5f)
    {
        delayLine.resize(maxDelaySamples);
        std::fill(delayLine.begin(), delayLine.end(), 0.0f);
        writeIndex = 0;
        this->delaySamples = juce::jlimit(1, maxDelaySamples, maxDelaySamples);
        this->apGain = initialGain;
        prepared = true;
    }

    void setDelaySamples(int newDelay)
    {
        delaySamples = juce::jlimit(1, (int)delayLine.size(), newDelay);
    }

    void setGain(float newGain)
    {
        apGain = juce::jlimit(-0.99f, 0.99f, newGain);
    }

    void reset()
    {
        std::fill(delayLine.begin(), delayLine.end(), 0.0f);
        writeIndex = 0;
    }

    // Scalar version - exact SHARC all-pass
    // CRITICAL: Read delayed sample FIRST, then write new value
    void processBlockScalar(const float* input, float* output, int numSamples) noexcept
    {
        if (!prepared) return;

        auto* buffer = delayLine.data();
        int idx = writeIndex;
        const int len = delaySamples;
        const float g = apGain;

        for (int i = 0; i < numSamples; ++i)
        {
            // 1. READ old delayed sample
            float delayed = buffer[idx];

            // 2. Calculate output: y[n] = -g*x[n] + x[n-M] + g*y[n-M]
            //    Simplified: out = -g*input + delayed (since delayed already contains x[n-M] + g*y[n-M-M])
            float out = -g * input[i] + delayed;

            // 3. WRITE new value: x[n] + g*y[n-M]
            buffer[idx] = input[i] + delayed * g;

            // 4. Output result
            output[i] = out;

            // 5. Advance circular buffer
            if (++idx >= len) idx = 0;
        }

        writeIndex = idx;
    }

    // SIMD version
    void processBlockSIMD(const float* input, float* output, int numSamples) noexcept
    {
        if (!prepared) return;

        using SIMD = juce::dsp::SIMDRegister<float>;
        constexpr size_t simdWidth = SIMD::SIMDRegister::size();

        auto* buffer = delayLine.data();
        int idx = writeIndex;
        const int len = delaySamples;
        const float g = apGain;

        size_t vectorSamples = (static_cast<size_t>(numSamples) / simdWidth) * simdWidth;

        // SIMD main loop
        for (size_t i = 0; i < vectorSamples; i += simdWidth)
        {
            alignas(32) float delayed[8] = { 0 };
            for (size_t j = 0; j < simdWidth; ++j)
                delayed[j] = buffer[(idx + static_cast<int>(j)) % len];

            SIMD delayedVec = SIMD::fromRawArray(delayed);
            SIMD inputVec = SIMD::fromRawArray(input + i);
            SIMD gVec(g);

            SIMD outVec = gVec * inputVec * (-1.0f) + delayedVec; // Fixed: proper SIMD ops
            outVec.copyToRawArray(output + i);

            // Store new values
            SIMD newVals = inputVec + gVec * delayedVec; // Fixed: proper SIMD multiplication
            alignas(32) float toStore[8] = { 0 };
            newVals.copyToRawArray(toStore);
            for (size_t j = 0; j < simdWidth; ++j)
                buffer[(idx + static_cast<int>(j)) % len] = toStore[j];

            idx = (idx + static_cast<int>(simdWidth)) % len;
        }

        // Scalar tail
        for (int i = static_cast<int>(vectorSamples); i < numSamples; ++i)
        {
            float delayed = buffer[idx];
            float out = -g * input[i] + delayed;
            buffer[idx] = input[i] + delayed * g;
            output[i] = out;
            if (++idx >= len) idx = 0;
        }

        writeIndex = idx;
    }

private:
    std::vector<float> delayLine;
    int delaySamples = 500;
    int writeIndex = 0;
    float apGain = 0.5f;
    bool prepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SharcAllpassFilter)
};

//==============================================================================
// Main Plugin Processor
//==============================================================================
class DdxReverbAudioProcessor : public juce::AudioProcessor
{
public:
    DdxReverbAudioProcessor();
    ~DdxReverbAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "DDX3216 Cathedral Reverb"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 20.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    // CPU monitoring
    float getCpuUsage() const { return static_cast<float>(cpuUsage); }

private:
    static constexpr int numCombs = 4;
    static constexpr int numAllpasses = 8;

    // Prime-number delays at 48kHz (classic Schroeder approach)
    static const int combDelays[numCombs];
    static const int allpassDelays[numAllpasses];

    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::array<SharcCombFilter, numCombs> combs;
    std::array<SharcAllpassFilter, numAllpasses> allpasses;

    // Pre-delay line
    std::vector<float> preDelayBuffer;
    int preDelayWritePos = 0;
    int preDelaySamples = 0;

    // Dry/wet buffers
    juce::AudioBuffer<float> dryBuffer;
    juce::AudioBuffer<float> tempBuffer;

    double currentSampleRate = 48000.0;
    bool useSIMD = false;

    // CPU monitoring
    double cpuUsage = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DdxReverbAudioProcessor)
};
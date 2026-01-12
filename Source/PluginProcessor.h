// --- START OF FILE PluginProcessor.h ---

#pragma once
#include <JuceHeader.h>
#include "DspEngine.h"

class NextGenSaturationAudioProcessor : public juce::AudioProcessor
{
public:
    NextGenSaturationAudioProcessor();
    ~NextGenSaturationAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;

    static constexpr int scopeSize = 1024;
    juce::AbstractFifo scopeFifo{ scopeSize };
    std::vector<float> scopeDataInput;
    std::vector<float> scopeDataOutput;

    std::atomic<float> currentInputRMS{ 0.0f };
    std::atomic<float> currentOutputRMS{ 0.0f };

    std::atomic<bool> isAutoGainLearning{ false };

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    int currentQuality = -1;
    double lastDspSampleRate = 0.0;

    // Filters
    HighPrecisionFilter preLowL, preLowR;
    HighPrecisionFilter preHighL, preHighR;
    HighPrecisionFilter postLowL, postLowR;
    HighPrecisionFilter postHighL, postHighR;

    // Saturation Engines
    SaturationCore satCoreL, satCoreR;

    // Dry Signal Delay Compensation
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> dryDelayL, dryDelayR;

    // Parameter Smoothers
    juce::LinearSmoothedValue<float> s_inputGain, s_drive, s_character, s_mix, s_outputGain;
    juce::LinearSmoothedValue<float> s_preLow, s_preHigh, s_postLow, s_postHigh;

    // Visualization
    int visSkipCounter = 0;

    // Auto Gain Variables
    double agRmsSumIn = 0.0;
    double agRmsSumOut = 0.0;
    double agMaxPeakIn = 0.0;
    int64_t agSampleCountIn = 0;
    int64_t agSampleCountOut = 0;
    int64_t agTotalSamplesProcessed = 0;
    int64_t agTargetSamples = 0;
    bool agWasLearning = false;

    void updateDspParameters();
    void updateOversampler(int qualityID, int samplesPerBlock);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NextGenSaturationAudioProcessor)
};
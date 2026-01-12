// --- START OF FILE PluginProcessor.cpp ---

#include "PluginProcessor.h"
#include "PluginEditor.h"

NextGenSaturationAudioProcessor::NextGenSaturationAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, &undoManager, "Parameters", createParameterLayout())
{
    scopeDataInput.resize(scopeSize, 0.0f);
    scopeDataOutput.resize(scopeSize, 0.0f);
    updateOversampler(1, 512);
}

NextGenSaturationAudioProcessor::~NextGenSaturationAudioProcessor() {}

bool NextGenSaturationAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    return true;
}

juce::AudioProcessorValueTreeState::ParameterLayout NextGenSaturationAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto createFloat = [&](juce::String id, juce::String name, float min, float max, float def) {
        params.push_back(std::make_unique<juce::AudioParameterFloat>(id, name, min, max, def));
        };
    auto createFreq = [&](juce::String id, juce::String name, float def) {
        auto range = juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.3f);
        params.push_back(std::make_unique<juce::AudioParameterFloat>(id, name, range, def));
        };

    createFloat("inputGain", "Input", -18.0f, 18.0f, 0.0f);
    params.push_back(std::make_unique<juce::AudioParameterBool>("autoGain", "Auto Gain", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>("bypass", "Bypass", false));

    createFreq("preLowCut", "Pre Low Cut", 20.0f);
    createFreq("preHighCut", "Pre High Cut", 20000.0f);

    juce::StringArray satTypes{
        "Analog Tape", "Tube Triode", "Tube Pentode", "Transformer", "Console",
        "JFET", "BJT", "Diode",
        "Soft Tanh", "Hard Clip", "Wavefold", "Rectify", "Bitcrush", "Exciter"
    };
    params.push_back(std::make_unique<juce::AudioParameterChoice>("satType", "Algorithm", satTypes, 0));
    createFloat("drive", "Drive", 0.0f, 24.0f, 0.0f);
    createFloat("character", "Character", 0.0f, 1.0f, 0.5f);

    juce::StringArray osQualities{ "Off", "2x", "4x", "8x", "16x (Ultra)" };
    params.push_back(std::make_unique<juce::AudioParameterChoice>("quality", "Quality", osQualities, 1));

    createFreq("postLowCut", "Post Low Cut", 20.0f);
    createFreq("postHighCut", "Post High Cut", 20000.0f);
    juce::StringArray slopes{ "6 dB/oct", "12 dB/oct", "24 dB/oct", "48 dB/oct" };
    params.push_back(std::make_unique<juce::AudioParameterChoice>("postSlope", "Slope", slopes, 1));

    createFloat("mix", "Dry/Wet", 0.0f, 100.0f, 100.0f);
    createFloat("outputGain", "Output", -18.0f, 18.0f, 0.0f);
    params.push_back(std::make_unique<juce::AudioParameterBool>("safetyClip", "Safety Clipper", true));

    return { params.begin(), params.end() };
}

void NextGenSaturationAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    lastDspSampleRate = 0.0;
    visSkipCounter = 0;

    preLowL.prepare(sampleRate); preLowR.prepare(sampleRate);
    preHighL.prepare(sampleRate); preHighR.prepare(sampleRate);
    postLowL.prepare(sampleRate); postLowR.prepare(sampleRate);
    postHighL.prepare(sampleRate); postHighR.prepare(sampleRate);

    satCoreL.reset(); satCoreR.reset();
    satCoreL.prepare(sampleRate);
    satCoreR.prepare(sampleRate);

    dryDelayL.prepare({ sampleRate, (juce::uint32)samplesPerBlock, 1 });
    dryDelayR.prepare({ sampleRate, (juce::uint32)samplesPerBlock, 1 });
    dryDelayL.setMaximumDelayInSamples(16384);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = samplesPerBlock;
    spec.numChannels = 1;

    s_inputGain.reset(sampleRate, 0.05);
    s_drive.reset(sampleRate, 0.05);
    s_character.reset(sampleRate, 0.05);
    s_mix.reset(sampleRate, 0.05);
    s_outputGain.reset(sampleRate, 0.05);
    s_preLow.reset(sampleRate, 0.05); s_preHigh.reset(sampleRate, 0.05);
    s_postLow.reset(sampleRate, 0.05); s_postHigh.reset(sampleRate, 0.05);

    int quality = (int)apvts.getRawParameterValue("quality")->load();
    updateOversampler(quality, samplesPerBlock);

    agWasLearning = false;
    isAutoGainLearning = false;
}

void NextGenSaturationAudioProcessor::updateOversampler(int qualityID, int samplesPerBlock)
{
    if (currentQuality == qualityID) return;
    currentQuality = qualityID;

    if (qualityID == 0) {
        oversampler.reset();
        setLatencySamples(0);
    }
    else {
        oversampler = std::make_unique<juce::dsp::Oversampling<float>>(2, qualityID, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true);
        oversampler->initProcessing(samplesPerBlock);
        setLatencySamples(oversampler->getLatencyInSamples());
    }
}

void NextGenSaturationAudioProcessor::updateDspParameters()
{
    float inGain = *apvts.getRawParameterValue("inputGain");
    float drv = *apvts.getRawParameterValue("drive");
    float chr = *apvts.getRawParameterValue("character");
    float mx = *apvts.getRawParameterValue("mix");
    float outGain = *apvts.getRawParameterValue("outputGain");

    s_inputGain.setTargetValue(juce::Decibels::decibelsToGain(inGain));
    s_drive.setTargetValue(drv);
    s_character.setTargetValue(chr);
    s_mix.setTargetValue(mx * 0.01f);
    s_outputGain.setTargetValue(juce::Decibels::decibelsToGain(outGain));

    s_preLow.setTargetValue(*apvts.getRawParameterValue("preLowCut"));
    s_preHigh.setTargetValue(*apvts.getRawParameterValue("preHighCut"));
    s_postLow.setTargetValue(*apvts.getRawParameterValue("postLowCut"));
    s_postHigh.setTargetValue(*apvts.getRawParameterValue("postHighCut"));
}

void NextGenSaturationAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    updateDspParameters();

    bool isBypassed = *apvts.getRawParameterValue("bypass") > 0.5f;
    if (isBypassed) {
        float inG = s_inputGain.getTargetValue();
        buffer.applyGain(inG);

        // FIX: Initialize variables
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        scopeFifo.prepareToWrite(buffer.getNumSamples(), start1, size1, start2, size2);
        const float* dL = buffer.getReadPointer(0);
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            if (++visSkipCounter >= 8) {
                visSkipCounter = 0;
                if (size1 > 0) {
                    if (start1 < scopeSize) {
                        scopeDataInput[start1] = dL[i];
                        scopeDataOutput[start1] = dL[i];
                    }
                    start1++; size1--;
                }
                else if (size2 > 0) {
                    if (start2 < scopeSize) {
                        scopeDataInput[start2] = dL[i];
                        scopeDataOutput[start2] = dL[i];
                    }
                    start2++; size2--;
                }
            }
        }
        scopeFifo.finishedWrite(buffer.getNumSamples() / 8);
        return;
    }

    int quality = (int)*apvts.getRawParameterValue("quality");
    updateOversampler(quality, buffer.getNumSamples());

    int postSlopeIdx = (int)*apvts.getRawParameterValue("postSlope");
    HighPrecisionFilter::Slope postSlope = (HighPrecisionFilter::Slope)postSlopeIdx;
    int satType = (int)*apvts.getRawParameterValue("satType");
    bool safety = *apvts.getRawParameterValue("safetyClip") > 0.5f;

    bool isLearning = *apvts.getRawParameterValue("autoGain") > 0.5f;
    isAutoGainLearning.store(isLearning);

    if (isLearning) {
        if (!agWasLearning) {
            agWasLearning = true;
            agRmsSumIn = 0.0;
            agRmsSumOut = 0.0;
            agMaxPeakIn = 0.0;
            agSampleCountIn = 0;
            agSampleCountOut = 0;
            agTotalSamplesProcessed = 0;
            agTargetSamples = (int64_t)(getSampleRate() * 3.0);
        }
    }
    else {
        agWasLearning = false;
    }

    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::AudioBlock<float> wetBlock = block;
    juce::dsp::AudioBlock<float> upsampledBlock;

    juce::AudioBuffer<float> dryBuffer;
    dryBuffer.makeCopyOf(buffer);

    if (isLearning) {
        const float* inL = dryBuffer.getReadPointer(0);
        const float* inR = (dryBuffer.getNumChannels() > 1) ? dryBuffer.getReadPointer(1) : nullptr;
        double threshold = 0.001;

        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            float sL = inL[i];
            float sR = (inR) ? inR[i] : sL;
            float gain = s_inputGain.getCurrentValue();
            sL *= gain; sR *= gain;

            double peak = std::max(std::abs(sL), std::abs(sR));
            if (peak > agMaxPeakIn) agMaxPeakIn = peak;

            if (std::abs(sL) > threshold) { agRmsSumIn += (double)sL * sL; agSampleCountIn++; }
            if (std::abs(sR) > threshold) { agRmsSumIn += (double)sR * sR; agSampleCountIn++; }
        }
    }

    float latency = (oversampler) ? oversampler->getLatencyInSamples() : 0.0f;

    {
        auto* dryL = dryBuffer.getWritePointer(0);
        auto* dryR = dryBuffer.getWritePointer(1);
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            dryDelayL.pushSample(0, dryL[i]);
            dryDelayR.pushSample(0, dryR[i]);
            dryL[i] = dryDelayL.popSample(0, latency);
            dryR[i] = dryDelayR.popSample(0, latency);
        }
    }

    if (oversampler) {
        upsampledBlock = oversampler->processSamplesUp(block);
        wetBlock = upsampledBlock;
    }

    auto numSamples = wetBlock.getNumSamples();
    auto* ptrL = wetBlock.getChannelPointer(0);
    auto* ptrR = wetBlock.getChannelPointer(1);

    double dspSampleRate = getSampleRate() * (oversampler ? oversampler->getOversamplingFactor() : 1.0);

    if (std::abs(dspSampleRate - lastDspSampleRate) > 1.0) {
        lastDspSampleRate = dspSampleRate;
        preLowL.prepare(dspSampleRate); preLowR.prepare(dspSampleRate);
        preHighL.prepare(dspSampleRate); preHighR.prepare(dspSampleRate);
        postLowL.prepare(dspSampleRate); postLowR.prepare(dspSampleRate);
        postHighL.prepare(dspSampleRate); postHighR.prepare(dspSampleRate);

        satCoreL.prepare(dspSampleRate);
        satCoreR.prepare(dspSampleRate);
        satCoreL.reset(); satCoreR.reset();
    }

    int updateCounter = 0;

    for (size_t i = 0; i < numSamples; ++i) {
        float inG = s_inputGain.getNextValue();
        float drv = s_drive.getNextValue();
        float chr = s_character.getNextValue();
        double preLC = s_preLow.getNextValue();
        double preHC = s_preHigh.getNextValue();
        double postLC = s_postLow.getNextValue();
        double postHC = s_postHigh.getNextValue();

        if (updateCounter == 0) {
            preLowL.setParams(HighPrecisionFilter::HighPass, preLC, HighPrecisionFilter::Slope12dB);
            preLowR.setParams(HighPrecisionFilter::HighPass, preLC, HighPrecisionFilter::Slope12dB);
            preHighL.setParams(HighPrecisionFilter::LowPass, preHC, HighPrecisionFilter::Slope12dB);
            preHighR.setParams(HighPrecisionFilter::LowPass, preHC, HighPrecisionFilter::Slope12dB);

            postLowL.setParams(HighPrecisionFilter::HighPass, postLC, postSlope);
            postLowR.setParams(HighPrecisionFilter::HighPass, postLC, postSlope);
            postHighL.setParams(HighPrecisionFilter::LowPass, postHC, postSlope);
            postHighR.setParams(HighPrecisionFilter::LowPass, postHC, postSlope);
        }
        updateCounter = (updateCounter + 1) & 7;

        double xL = (double)ptrL[i] * inG;
        double xR = (double)ptrR[i] * inG;

        xL = preLowL.process(xL);
        xR = preLowR.process(xR);
        xL = preHighL.process(xL);
        xR = preHighR.process(xR);

        xL = satCoreL.process(xL, satType, drv, chr);
        xR = satCoreR.process(xR, satType, drv, chr);

        xL = postLowL.process(xL);
        xR = postLowR.process(xR);
        xL = postHighL.process(xL);
        xR = postHighR.process(xR);

        ptrL[i] = (float)xL;
        ptrR[i] = (float)xR;
    }

    if (oversampler) {
        oversampler->processSamplesDown(block);
    }

    auto* outL = buffer.getWritePointer(0);
    auto* outR = buffer.getWritePointer(1);
    const auto* dL = dryBuffer.getReadPointer(0);
    const auto* dR = dryBuffer.getReadPointer(1);

    float localMaxIn = 0.0f;
    float localMaxOut = 0.0f;

    // FIX: Initialize variables
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    scopeFifo.prepareToWrite(buffer.getNumSamples(), start1, size1, start2, size2);

    if (isLearning) {
        double threshold = 0.001;
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            float mix = s_mix.getCurrentValue();
            float wetL = outL[i];
            float wetR = outR[i];
            float mixedL = dL[i] * (1.0f - mix) + wetL * mix;
            float mixedR = dR[i] * (1.0f - mix) + wetR * mix;

            if (std::abs(mixedL) > threshold) { agRmsSumOut += (double)mixedL * mixedL; agSampleCountOut++; }
            if (std::abs(mixedR) > threshold) { agRmsSumOut += (double)mixedR * mixedR; agSampleCountOut++; }
        }

        agTotalSamplesProcessed += buffer.getNumSamples();

        if (agTotalSamplesProcessed >= agTargetSamples) {
            double rmsIn = (agSampleCountIn > 0) ? std::sqrt(agRmsSumIn / agSampleCountIn) : 0.0;
            double rmsOut = (agSampleCountOut > 0) ? std::sqrt(agRmsSumOut / agSampleCountOut) : 0.0;

            double targetPeak = 0.9885; // -0.1 dB
            if (agMaxPeakIn > targetPeak) {
                double peakDiffDB = juce::Decibels::gainToDecibels(agMaxPeakIn) - (-0.1);
                float newInputDB = 0.0f - (float)peakDiffDB;

                if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("inputGain"))) {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost(p->convertTo0to1(newInputDB));
                    p->endChangeGesture();
                }
            }

            if (rmsIn > 0.0001 && rmsOut > 0.0001) {
                double ratio = rmsIn / rmsOut;
                double dbDiff = juce::Decibels::gainToDecibels(ratio);

                float newOutDB = (float)dbDiff;
                newOutDB = juce::jlimit(-18.0f, 18.0f, newOutDB);

                if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("outputGain"))) {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost(p->convertTo0to1(newOutDB));
                    p->endChangeGesture();
                }
            }

            if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("autoGain"))) {
                p->beginChangeGesture();
                p->setValueNotifyingHost(0.0f);
                p->endChangeGesture();
            }
        }
    }

    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        float mix = s_mix.getNextValue();
        float outG = s_outputGain.getNextValue();

        float wetL = outL[i];
        float wetR = outR[i];

        float mixedL = dL[i] * (1.0f - mix) + wetL * mix;
        float mixedR = dR[i] * (1.0f - mix) + wetR * mix;

        mixedL *= outG;
        mixedR *= outG;

        if (safety) {
            mixedL = juce::jlimit(-1.0f, 1.0f, mixedL);
            mixedR = juce::jlimit(-1.0f, 1.0f, mixedR);
        }

        outL[i] = mixedL;
        outR[i] = mixedR;

        if (++visSkipCounter >= 8) {
            visSkipCounter = 0;
            if (size1 > 0) {
                if (start1 < scopeSize) {
                    scopeDataInput[start1] = dL[i];
                    scopeDataOutput[start1] = mixedL;
                }
                start1++; size1--;
            }
            else if (size2 > 0) {
                if (start2 < scopeSize) {
                    scopeDataInput[start2] = dL[i];
                    scopeDataOutput[start2] = mixedL;
                }
                start2++; size2--;
            }
        }

        localMaxIn = std::max(localMaxIn, std::abs(dL[i]));
        localMaxOut = std::max(localMaxOut, std::abs(mixedL));
    }

    scopeFifo.finishedWrite(buffer.getNumSamples() / 8);

    currentInputRMS.store(std::max(currentInputRMS.load() * 0.9f, localMaxIn));
    currentOutputRMS.store(std::max(currentOutputRMS.load() * 0.9f, localMaxOut));
}

const juce::String NextGenSaturationAudioProcessor::getName() const { return JucePlugin_Name; }
bool NextGenSaturationAudioProcessor::acceptsMidi() const { return false; }
bool NextGenSaturationAudioProcessor::producesMidi() const { return false; }
bool NextGenSaturationAudioProcessor::isMidiEffect() const { return false; }
double NextGenSaturationAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int NextGenSaturationAudioProcessor::getNumPrograms() { return 1; }
int NextGenSaturationAudioProcessor::getCurrentProgram() { return 0; }
void NextGenSaturationAudioProcessor::setCurrentProgram(int index) {}
const juce::String NextGenSaturationAudioProcessor::getProgramName(int index) { return {}; }
void NextGenSaturationAudioProcessor::changeProgramName(int index, const juce::String& newName) {}
void NextGenSaturationAudioProcessor::releaseResources() {}
bool NextGenSaturationAudioProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* NextGenSaturationAudioProcessor::createEditor() { return new NextGenSaturationAudioProcessorEditor(*this); }
void NextGenSaturationAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}
void NextGenSaturationAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState.get() != nullptr) apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new NextGenSaturationAudioProcessor(); }
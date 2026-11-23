#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

SineFairyAudioProcessor::SineFairyAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "SineFairyParams", createParameterLayout())
{
    pMinF0       = apvts.getRawParameterValue("minF0");
    pMaxF0       = apvts.getRawParameterValue("maxF0");
    pTrackSpeed  = apvts.getRawParameterValue("trackSpeed");

    pSlope       = apvts.getRawParameterValue("lowCutSlope");
    pOffsetSt    = apvts.getRawParameterValue("lowCutOffset");
    pFloorHz     = apvts.getRawParameterValue("lowCutFloor");

    pResynthMode = apvts.getRawParameterValue("resynthMode");
    pNumHarm     = apvts.getRawParameterValue("numHarmonics");
    pResynthMix  = apvts.getRawParameterValue("resynthMix");
    pPurityBw    = apvts.getRawParameterValue("purityBw");
    pH1Gain      = apvts.getRawParameterValue("h1Gain");
    pH2Gain      = apvts.getRawParameterValue("h2Gain");
    pH3Gain      = apvts.getRawParameterValue("h3Gain");
    pH4Gain      = apvts.getRawParameterValue("h4Gain");

    pAudition    = apvts.getRawParameterValue("audition");
    pOutGain     = apvts.getRawParameterValue("outGain");
    pBypass      = apvts.getRawParameterValue("bypass");

    for (auto& a : harmAmpAtomic)
        a.store(0.0f, std::memory_order_relaxed);
}

SineFairyAudioProcessor::~SineFairyAudioProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout SineFairyAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // 1. Pitch Tracker
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "minF0", 1 }, "Min Pitch",
        juce::NormalisableRange<float>(45.0f, 300.0f, 1.0f, 0.5f), 75.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "maxF0", 1 }, "Max Pitch",
        juce::NormalisableRange<float>(250.0f, 1200.0f, 1.0f, 0.5f), 650.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "trackSpeed", 1 }, "Track Speed",
        juce::NormalisableRange<float>(4.0f, 80.0f, 0.5f, 0.6f), 16.0f));

    // 2. Pitch-Tracking Low-Cut Filter
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "lowCutSlope", 1 }, "Low-Cut Slope",
        juce::StringArray { "OFF", "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" }, 2));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "lowCutOffset", 1 }, "Low-Cut Offset",
        juce::NormalisableRange<float>(-12.0f, 0.0f, 0.1f), -4.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "lowCutFloor", 1 }, "Unvoiced Floor",
        juce::NormalisableRange<float>(20.0f, 250.0f, 1.0f, 0.5f), 65.0f));

    // 3. Phase-Locked Harmonic Sine Resynthesis
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "resynthMode", 1 }, "Resynth Mode",
        juce::StringArray { "CLEAN (Replace)", "ENHANCE (Parallel)" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { "numHarmonics", 1 }, "Harmonics Count", 1, 6, 4));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "resynthMix", 1 }, "Harmonic Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.5f), 65.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "purityBw", 1 }, "Lock-In BW",
        juce::NormalisableRange<float>(5.0f, 35.0f, 0.2f, 0.7f), 11.0f));

    // Individual Low-Harmonic Level Trims (0% .. 200%, default 100% unity)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "h1Gain", 1 }, "H1 Level",
        juce::NormalisableRange<float>(0.0f, 200.0f, 1.0f), 100.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "h2Gain", 1 }, "H2 Level",
        juce::NormalisableRange<float>(0.0f, 200.0f, 1.0f), 100.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "h3Gain", 1 }, "H3 Level",
        juce::NormalisableRange<float>(0.0f, 200.0f, 1.0f), 100.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "h4Gain", 1 }, "H4 Level",
        juce::NormalisableRange<float>(0.0f, 200.0f, 1.0f), 100.0f));

    // 4. Output & Audition
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "audition", 1 }, "Audition",
        juce::StringArray { "NORMAL", "SOLO SINES", "DELTA (NOISE)" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "outGain", 1 }, "Output Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "bypass", 1 }, "Bypass", false));

    return { params.begin(), params.end() };
}

void SineFairyAudioProcessor::prepareToPlay(double sampleRate, int)
{
    const double fs = std::max(sampleRate, 8000.0);
    currentFsAtomic.store(fs, std::memory_order_relaxed);

    engine.prepare(fs);
    inputScope.reset();
    outputScope.reset();

    const double rampSec = 0.015;
    smOffsetSt.reset(fs, rampSec);
    smFloorHz.reset(fs, rampSec);
    smResynthMix.reset(fs, rampSec);
    smPurityBw.reset(fs, rampSec);
    smOutGain.reset(fs, rampSec);
    smH1.reset(fs, rampSec);
    smH2.reset(fs, rampSec);
    smH3.reset(fs, rampSec);
    smH4.reset(fs, rampSec);

    smOffsetSt.setCurrentAndTargetValue(pOffsetSt->load());
    smFloorHz.setCurrentAndTargetValue(pFloorHz->load());
    smResynthMix.setCurrentAndTargetValue(pResynthMix->load() * 0.01);
    smPurityBw.setCurrentAndTargetValue(pPurityBw->load());
    smOutGain.setCurrentAndTargetValue(pOutGain->load());
    smH1.setCurrentAndTargetValue(pH1Gain->load() * 0.01);
    smH2.setCurrentAndTargetValue(pH2Gain->load() * 0.01);
    smH3.setCurrentAndTargetValue(pH3Gain->load() * 0.01);
    smH4.setCurrentAndTargetValue(pH4Gain->load() * 0.01);

    setLatencySamples(0); // True zero-latency audio path!
}

void SineFairyAudioProcessor::releaseResources()
{
    engine.reset();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SineFairyAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto outSet = layouts.getMainOutputChannelSet();
    const auto inSet  = layouts.getMainInputChannelSet();
    if (outSet != juce::AudioChannelSet::mono() && outSet != juce::AudioChannelSet::stereo())
        return false;
    return outSet == inSet;
}
#endif

void SineFairyAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numInCh    = getTotalNumInputChannels();
    const int numOutCh   = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    for (int i = numInCh; i < numOutCh; ++i)
        buffer.clear(i, 0, numSamples);

    if (numSamples <= 0 || numInCh <= 0)
        return;

    smOffsetSt.setTargetValue(pOffsetSt->load());
    smFloorHz.setTargetValue(pFloorHz->load());
    smResynthMix.setTargetValue(pResynthMix->load() * 0.01);
    smPurityBw.setTargetValue(pPurityBw->load());
    smOutGain.setTargetValue(pOutGain->load());
    smH1.setTargetValue(pH1Gain->load() * 0.01);
    smH2.setTargetValue(pH2Gain->load() * 0.01);
    smH3.setTargetValue(pH3Gain->load() * 0.01);
    smH4.setTargetValue(pH4Gain->load() * 0.01);

    sinefairy::EngineParams p;
    p.minF0Hz       = pMinF0->load();
    p.maxF0Hz       = pMaxF0->load();
    p.trackSpeedMs  = pTrackSpeed->load();
    p.lowCutSlope   = static_cast<sinefairy::LowCutSlope>(static_cast<int>(std::round(pSlope->load())));
    p.resynthMode   = static_cast<sinefairy::ResynthMode>(static_cast<int>(std::round(pResynthMode->load())));
    p.numHarmonics  = static_cast<int>(std::round(pNumHarm->load()));
    p.audition      = static_cast<sinefairy::AuditionMode>(static_cast<int>(std::round(pAudition->load())));
    p.bypass        = pBypass->load() > 0.5f;

    const bool isMono = (numInCh == 1);
    float* chL = buffer.getWritePointer(0);
    float* chR = isMono ? nullptr : buffer.getWritePointer(1);

    for (int i = 0; i < numSamples; ++i)
    {
        p.lowCutOffsetSt = smOffsetSt.getNextValue();
        p.lowCutFloorHz  = smFloorHz.getNextValue();
        p.resynthMix     = smResynthMix.getNextValue();
        p.purityBwHz     = smPurityBw.getNextValue();
        p.outputGainDb   = smOutGain.getNextValue();
        p.harmGains      = { smH1.getNextValue(), smH2.getNextValue(),
                             smH3.getNextValue(), smH4.getNextValue(), 1.0, 1.0 };

        double l = static_cast<double>(chL[i]);
        double r = isMono ? l : static_cast<double>(chR[i]);

        const float inMono = static_cast<float>(0.5 * (l + r));
        engine.processStereoSample(l, r, p);
        const float outMono = static_cast<float>(0.5 * (l + r));

        chL[i] = static_cast<float>(l);
        if (!isMono)
            chR[i] = static_cast<float>(r);

        inputScope.push(inMono);
        outputScope.push(outMono);
    }

    trackedF0Atomic.store(static_cast<float>(engine.getTrackedF0()), std::memory_order_relaxed);
    confidenceAtomic.store(static_cast<float>(engine.getConfidence()), std::memory_order_relaxed);
    lowCutHzAtomic.store(static_cast<float>(engine.getLowCutCutoffHz()), std::memory_order_relaxed);
    xoverHzAtomic.store(static_cast<float>(engine.getXoverHz()), std::memory_order_relaxed);

    for (int k = 0; k < 6; ++k)
        harmAmpAtomic[static_cast<size_t>(k)].store(
            static_cast<float>(engine.getHarmonicAmp(k)), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* SineFairyAudioProcessor::createEditor()
{
    return new SineFairyAudioProcessorEditor(*this);
}

void SineFairyAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void SineFairyAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SineFairyAudioProcessor();
}

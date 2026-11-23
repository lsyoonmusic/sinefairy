#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

SineFairyAudioProcessor::SineFairyAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "SineFairyV11Params", createParameterLayout())
{
    pMinF0       = apvts.getRawParameterValue("minF0");
    pMaxF0       = apvts.getRawParameterValue("maxF0");
    pSlope       = apvts.getRawParameterValue("lowCutSlope");
    pPhaseMode   = apvts.getRawParameterValue("lowCutPhase");
    pOffsetSt    = apvts.getRawParameterValue("lowCutOffset");
    pFloorHz     = apvts.getRawParameterValue("lowCutFloor");

    pResynthMode = apvts.getRawParameterValue("resynthMode");
    pNumHarm     = apvts.getRawParameterValue("numHarmonics");
    pResynthMix  = apvts.getRawParameterValue("resynthMix");
    pPurity      = apvts.getRawParameterValue("purityPct");
    pH1Gain      = apvts.getRawParameterValue("h1Gain");
    pH2Gain      = apvts.getRawParameterValue("h2Gain");
    pH34Gain     = apvts.getRawParameterValue("h34Gain");
    pH5UpGain    = apvts.getRawParameterValue("h5UpGain");

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

    // 1. Pitch Range & Surgical Tracking Low-Cut
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "minF0", 1 }, "Min Pitch",
        juce::NormalisableRange<float>(45.0f, 300.0f, 1.0f, 0.5f), 75.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "maxF0", 1 }, "Max Pitch",
        juce::NormalisableRange<float>(250.0f, 1200.0f, 1.0f, 0.5f), 680.0f));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "lowCutSlope", 1 }, "Low-Cut Slope",
        juce::StringArray { "OFF", "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct", "BRICKWALL" }, 3));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "lowCutPhase", 1 }, "Low-Cut Phase",
        juce::StringArray { "LINEAR PHASE", "MIN PHASE" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "lowCutOffset", 1 }, "Low-Cut Offset",
        juce::NormalisableRange<float>(-12.0f, 0.0f, 0.1f), -3.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "lowCutFloor", 1 }, "Unvoiced Floor",
        juce::NormalisableRange<float>(20.0f, 250.0f, 1.0f, 0.5f), 65.0f));

    // 2. HQ Lookahead Phase-Locked Harmonic Reconstructor (up to 20 harmonics!)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "resynthMode", 1 }, "Resynth Mode",
        juce::StringArray { "CLEAN (Replace)", "ENHANCE (Parallel)" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { "numHarmonics", 1 }, "Harmonics Count", 1, 20, 8));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "resynthMix", 1 }, "Harmonic Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.5f), 65.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "purityPct", 1 }, "Purity Gate",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 55.0f));

    // 4 Musical Harmonic Zone Trims (0% .. 200%, 100% = unity)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "h1Gain", 1 }, "H1 Level",
        juce::NormalisableRange<float>(0.0f, 200.0f, 1.0f), 100.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "h2Gain", 1 }, "H2 Level",
        juce::NormalisableRange<float>(0.0f, 200.0f, 1.0f), 100.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "h34Gain", 1 }, "H3-H4 Level",
        juce::NormalisableRange<float>(0.0f, 200.0f, 1.0f), 100.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "h5UpGain", 1 }, "H5-H20 Level",
        juce::NormalisableRange<float>(0.0f, 200.0f, 1.0f), 100.0f));

    // 3. Audition & Output
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
    const int lat = engine.getLatencySamples();
    latencyAtomic.store(lat, std::memory_order_relaxed);
    setLatencySamples(lat); // Exact DAW PDC (4096 @ 48 kHz = 85.3 ms)

    inputScope.reset();
    outputScope.reset();

    const double rampSec = 0.015;
    smOffsetSt.reset(fs, rampSec);
    smFloorHz.reset(fs, rampSec);
    smResynthMix.reset(fs, rampSec);
    smPurity.reset(fs, rampSec);
    smOutGain.reset(fs, rampSec);
    smH1.reset(fs, rampSec);
    smH2.reset(fs, rampSec);
    smH34.reset(fs, rampSec);
    smH5Up.reset(fs, rampSec);

    smOffsetSt.setCurrentAndTargetValue(pOffsetSt->load());
    smFloorHz.setCurrentAndTargetValue(pFloorHz->load());
    smResynthMix.setCurrentAndTargetValue(pResynthMix->load() * 0.01);
    smPurity.setCurrentAndTargetValue(pPurity->load() * 0.01);
    smOutGain.setCurrentAndTargetValue(pOutGain->load());
    smH1.setCurrentAndTargetValue(pH1Gain->load() * 0.01);
    smH2.setCurrentAndTargetValue(pH2Gain->load() * 0.01);
    smH34.setCurrentAndTargetValue(pH34Gain->load() * 0.01);
    smH5Up.setCurrentAndTargetValue(pH5UpGain->load() * 0.01);
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
    smPurity.setTargetValue(pPurity->load() * 0.01);
    smOutGain.setTargetValue(pOutGain->load());
    smH1.setTargetValue(pH1Gain->load() * 0.01);
    smH2.setTargetValue(pH2Gain->load() * 0.01);
    smH34.setTargetValue(pH34Gain->load() * 0.01);
    smH5Up.setTargetValue(pH5UpGain->load() * 0.01);

    sinefairy::EngineParams p;
    p.minF0Hz       = pMinF0->load();
    p.maxF0Hz       = pMaxF0->load();
    p.lowCutSlope   = static_cast<sinefairy::LowCutSlope>(static_cast<int>(std::round(pSlope->load())));
    p.lowCutPhase   = static_cast<sinefairy::FilterPhaseMode>(static_cast<int>(std::round(pPhaseMode->load())));
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
        p.purity01       = smPurity.getNextValue();
        p.outputGainDb   = smOutGain.getNextValue();
        p.gH1            = smH1.getNextValue();
        p.gH2            = smH2.getNextValue();
        p.gH34           = smH34.getNextValue();
        p.gH5Up          = smH5Up.getNextValue();

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

    for (int k = 0; k < kMaxHarmonics; ++k)
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

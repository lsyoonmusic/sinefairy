#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "DSP/SineFairyV11Engine.h"
#include <array>
#include <atomic>

template <int Capacity>
class LockFreeAudioScope
{
public:
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");

    void reset() noexcept
    {
        buffer.fill(0.0f);
        writePos.store(0, std::memory_order_relaxed);
    }

    inline void push(float sample) noexcept
    {
        const int idx = writePos.load(std::memory_order_relaxed);
        buffer[static_cast<size_t>(idx)] = sample;
        writePos.store((idx + 1) & (Capacity - 1), std::memory_order_release);
    }

    void copyLatest(float* dest, int numSamples) const noexcept
    {
        const int end = writePos.load(std::memory_order_acquire);
        int start = (end - numSamples) & (Capacity - 1);
        for (int i = 0; i < numSamples; ++i)
        {
            dest[i] = buffer[static_cast<size_t>(start)];
            start = (start + 1) & (Capacity - 1);
        }
    }

private:
    std::array<float, Capacity> buffer {};
    std::atomic<int> writePos { 0 };
};

class SineFairyAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kScopeCapacity = 8192;
    static constexpr int kFftOrder      = 12;   // 4096-pt display FFT
    static constexpr int kFftSize       = 1 << kFftOrder;
    static constexpr int kMaxHarmonics  = sinefairy::kMaxHarmonics;

    SineFairyAudioProcessor();
    ~SineFairyAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
   #endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.18; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // Lock-free telemetry for GUI
    double getCurrentSampleRate() const noexcept    { return currentFsAtomic.load(std::memory_order_relaxed); }
    int    getEngineLatencySamples() const noexcept { return latencyAtomic.load(std::memory_order_relaxed); }
    float  getTrackedF0() const noexcept            { return trackedF0Atomic.load(std::memory_order_relaxed); }
    float  getConfidence() const noexcept           { return confidenceAtomic.load(std::memory_order_relaxed); }
    float  getLowCutCutoffHz() const noexcept       { return lowCutHzAtomic.load(std::memory_order_relaxed); }
    float  getXoverHz() const noexcept              { return xoverHzAtomic.load(std::memory_order_relaxed); }
    float  getHarmonicAmp(int k) const noexcept
    {
        return harmAmpAtomic[static_cast<size_t>(juce::jlimit(0, kMaxHarmonics - 1, k))].load(std::memory_order_relaxed);
    }

    const LockFreeAudioScope<kScopeCapacity>& getInputScope() const noexcept  { return inputScope; }
    const LockFreeAudioScope<kScopeCapacity>& getOutputScope() const noexcept { return outputScope; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    sinefairy::SineFairyV11Engine engine;

    // Smoothed continuous parameters
    juce::SmoothedValue<double> smOffsetSt, smFloorHz, smResynthMix, smPurity, smOutGain;
    juce::SmoothedValue<double> smH1, smH2, smH34, smH5Up;

    // Cached atomic parameter pointers
    std::atomic<float>* pMinF0       = nullptr;
    std::atomic<float>* pMaxF0       = nullptr;
    std::atomic<float>* pSlope       = nullptr;
    std::atomic<float>* pPhaseMode   = nullptr;
    std::atomic<float>* pOffsetSt    = nullptr;
    std::atomic<float>* pFloorHz     = nullptr;

    std::atomic<float>* pResynthMode = nullptr;
    std::atomic<float>* pNumHarm     = nullptr;
    std::atomic<float>* pResynthMix  = nullptr;
    std::atomic<float>* pPurity      = nullptr;
    std::atomic<float>* pH1Gain      = nullptr;
    std::atomic<float>* pH2Gain      = nullptr;
    std::atomic<float>* pH34Gain     = nullptr;
    std::atomic<float>* pH5UpGain    = nullptr;

    std::atomic<float>* pAudition    = nullptr;
    std::atomic<float>* pOutGain     = nullptr;
    std::atomic<float>* pBypass      = nullptr;

    // Lock-free telemetry atomics
    std::atomic<double> currentFsAtomic  { 48000.0 };
    std::atomic<int>    latencyAtomic    { 4096 };
    std::atomic<float>  trackedF0Atomic  { 180.0f };
    std::atomic<float>  confidenceAtomic { 0.0f };
    std::atomic<float>  lowCutHzAtomic   { 65.0f };
    std::atomic<float>  xoverHzAtomic    { 1800.0f };
    std::array<std::atomic<float>, kMaxHarmonics> harmAmpAtomic {};

    LockFreeAudioScope<kScopeCapacity> inputScope;
    LockFreeAudioScope<kScopeCapacity> outputScope;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SineFairyAudioProcessor)
};

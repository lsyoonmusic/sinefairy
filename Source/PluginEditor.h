#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "LookAndFeelSineFairy.h"

class SineFairyAudioProcessorEditor : public juce::AudioProcessorEditor,
                                      private juce::Timer
{
public:
    explicit SineFairyAudioProcessorEditor(SineFairyAudioProcessor&);
    ~SineFairyAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setupKnob(juce::Slider& s, const juce::String& suffix, uint32_t accentHex);

    SineFairyAudioProcessor& processorRef;
    LookAndFeelSineFairy laf;
    HarmonicSpectrumView spectrumView;

    // 1. Pitch Tracker Controls
    juce::Slider minF0Knob, maxF0Knob, trackSpeedKnob;

    // 2. Pitch-Tracking Low-Cut Controls
    juce::ComboBox slopeBox;
    juce::Slider offsetKnob, floorKnob;

    // 3. Harmonic Sine Reconstructor Controls
    juce::ComboBox resynthModeBox;
    juce::Slider numHarmKnob, resynthMixKnob, purityBwKnob;
    juce::Slider h1Knob, h2Knob, h3Knob, h4Knob;

    // 4. Bottom Audition & Utility Bar
    juce::ComboBox auditionBox;
    juce::Slider outGainKnob;
    juce::ToggleButton bypassBtn { "BYPASS" };

    using SAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SAtt> attMinF0, attMaxF0, attTrackSpeed;
    std::unique_ptr<CAtt> attSlope;
    std::unique_ptr<SAtt> attOffset, attFloor;
    std::unique_ptr<CAtt> attResynthMode;
    std::unique_ptr<SAtt> attNumHarm, attResynthMix, attPurityBw;
    std::unique_ptr<SAtt> attH1, attH2, attH3, attH4;
    std::unique_ptr<CAtt> attAudition;
    std::unique_ptr<SAtt> attOutGain;
    std::unique_ptr<BAtt> attBypass;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SineFairyAudioProcessorEditor)
};

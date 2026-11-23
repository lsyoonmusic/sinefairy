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

    // Card 1: Surgical Tracking Low-Cut & Pitch Range
    juce::ComboBox slopeBox, phaseModeBox;
    juce::Slider offsetKnob, floorKnob, minF0Knob, maxF0Knob;

    // Card 2: HQ Lookahead Phase-Locked 20-Harmonic Reconstructor
    juce::ComboBox resynthModeBox;
    juce::Slider numHarmKnob, resynthMixKnob, purityKnob;
    juce::Slider h1Knob, h2Knob, h34Knob, h5UpKnob;

    // Card 3: Audition & Output
    juce::ComboBox auditionBox;
    juce::Slider outGainKnob;
    juce::ToggleButton bypassBtn { "BYPASS" };

    using SAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<CAtt> attSlope, attPhaseMode;
    std::unique_ptr<SAtt> attOffset, attFloor, attMinF0, attMaxF0;
    std::unique_ptr<CAtt> attResynthMode;
    std::unique_ptr<SAtt> attNumHarm, attResynthMix, attPurity;
    std::unique_ptr<SAtt> attH1, attH2, attH34, attH5Up;
    std::unique_ptr<CAtt> attAudition;
    std::unique_ptr<SAtt> attOutGain;
    std::unique_ptr<BAtt> attBypass;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SineFairyAudioProcessorEditor)
};

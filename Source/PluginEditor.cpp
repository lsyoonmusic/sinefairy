#include "PluginEditor.h"

SineFairyAudioProcessorEditor::SineFairyAudioProcessorEditor(SineFairyAudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p), spectrumView(p)
{
    setLookAndFeel(&laf);
    setSize(900, 560);

    addAndMakeVisible(spectrumView);

    // 1. Pitch Tracker (Warm Gold accent)
    setupKnob(minF0Knob,      " Hz", 0xfff0c467);
    setupKnob(maxF0Knob,      " Hz", 0xfff0c467);
    setupKnob(trackSpeedKnob, " ms", 0xfff0c467);

    // 2. Pitch-Tracking Low-Cut (Coral Red accent)
    slopeBox.addItemList({ "OFF", "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" }, 1);
    addAndMakeVisible(slopeBox);
    setupKnob(offsetKnob, " st", 0xffff6e5c);
    setupKnob(floorKnob,  " Hz", 0xffff6e5c);

    // 3. Harmonic Sine Resynth (Cyan-Mint accent)
    resynthModeBox.addItemList({ "CLEAN (Replace)", "ENHANCE (Parallel)" }, 1);
    addAndMakeVisible(resynthModeBox);
    setupKnob(numHarmKnob,    "",    0xff4ae6d8);
    setupKnob(resynthMixKnob, " %",  0xff4ae6d8);
    setupKnob(purityBwKnob,   " Hz", 0xff4ae6d8);

    setupKnob(h1Knob, " %", 0xfff0c467);
    setupKnob(h2Knob, " %", 0xff5ce8dc);
    setupKnob(h3Knob, " %", 0xff5ce8dc);
    setupKnob(h4Knob, " %", 0xff5ce8dc);

    // 4. Bottom Audition & Utility Bar
    auditionBox.addItemList({ "NORMAL", "SOLO SINES", "DELTA (NOISE)" }, 1);
    addAndMakeVisible(auditionBox);
    setupKnob(outGainKnob, " dB", 0xffb8c9dc);
    outGainKnob.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 18);
    addAndMakeVisible(bypassBtn);

    auto& a = processorRef.apvts;
    attMinF0       = std::make_unique<SAtt>(a, "minF0",        minF0Knob);
    attMaxF0       = std::make_unique<SAtt>(a, "maxF0",        maxF0Knob);
    attTrackSpeed  = std::make_unique<SAtt>(a, "trackSpeed",   trackSpeedKnob);

    attSlope       = std::make_unique<CAtt>(a, "lowCutSlope",  slopeBox);
    attOffset      = std::make_unique<SAtt>(a, "lowCutOffset", offsetKnob);
    attFloor       = std::make_unique<SAtt>(a, "lowCutFloor",  floorKnob);

    attResynthMode = std::make_unique<CAtt>(a, "resynthMode",  resynthModeBox);
    attNumHarm     = std::make_unique<SAtt>(a, "numHarmonics", numHarmKnob);
    attResynthMix  = std::make_unique<SAtt>(a, "resynthMix",   resynthMixKnob);
    attPurityBw    = std::make_unique<SAtt>(a, "purityBw",     purityBwKnob);

    attH1          = std::make_unique<SAtt>(a, "h1Gain",       h1Knob);
    attH2          = std::make_unique<SAtt>(a, "h2Gain",       h2Knob);
    attH3          = std::make_unique<SAtt>(a, "h3Gain",       h3Knob);
    attH4          = std::make_unique<SAtt>(a, "h4Gain",       h4Knob);

    attAudition    = std::make_unique<CAtt>(a, "audition",     auditionBox);
    attOutGain     = std::make_unique<SAtt>(a, "outGain",      outGainKnob);
    attBypass      = std::make_unique<BAtt>(a, "bypass",       bypassBtn);

    startTimerHz(30);
}

SineFairyAudioProcessorEditor::~SineFairyAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void SineFairyAudioProcessorEditor::setupKnob(juce::Slider& s, const juce::String& suffix, uint32_t accentHex)
{
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 15);
    s.setTextValueSuffix(suffix);
    s.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffdbe6f2));
    s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0x550b0f14));
    s.getProperties().set(LookAndFeelSineFairy::kAccentColourProp, static_cast<int>(accentHex));
    s.setRotaryParameters(juce::degreesToRadians(220.0f), juce::degreesToRadians(500.0f), true);
    addAndMakeVisible(s);
}

void SineFairyAudioProcessorEditor::timerCallback()
{
    spectrumView.updateFrame(
        slopeBox.getSelectedItemIndex(),
        static_cast<int>(std::round(numHarmKnob.getValue())),
        resynthModeBox.getSelectedItemIndex(),
        static_cast<float>(resynthMixKnob.getValue())
    );
}

void SineFairyAudioProcessorEditor::paint(juce::Graphics& g)
{
    const float w = static_cast<float>(getWidth());
    const float h = static_cast<float>(getHeight());

    // Sleek midnight slate studio chassis
    juce::ColourGradient bg(juce::Colour(0xff191f28), 0.0f, 0.0f,
                            juce::Colour(0xff10141a), 0.0f, h, false);
    g.setGradientFill(bg);
    g.fillAll();

    // Header Bar
    g.setColour(juce::Colour(0xff0d1117));
    g.fillRect(0.0f, 0.0f, w, 42.0f);
    g.setColour(juce::Colour(0xff283444));
    g.drawHorizontalLine(42, 0.0f, w);

    g.setColour(juce::Colour(0xff678099));
    g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    g.drawText("ASIMORPH's FELLAS", 20, 8, 180, 12, juce::Justification::centredLeft);

    g.setColour(juce::Colour(0xfff0f6fc));
    g.setFont(juce::Font(juce::FontOptions(17.5f, juce::Font::bold)));
    g.drawText("SINE FAIRY", 20, 19, 180, 20, juce::Justification::centredLeft);

    g.setColour(juce::Colour(0xff52d6cb));
    g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    g.drawText("PITCH-TRACKING LOW-CUT  +  PHASE-LOCKED HARMONIC SINE DENOISER  |  ZERO LATENCY",
               210, 14, 670, 16, juce::Justification::centredRight);

    // Three Control Module Cards
    auto drawCard = [&g](juce::Rectangle<float> r, const juce::String& title, juce::Colour accent)
    {
        g.setColour(juce::Colour(0xff141a22));
        g.fillRoundedRectangle(r, 6.0f);
        g.setColour(juce::Colour(0xff2b3747));
        g.drawRoundedRectangle(r, 6.0f, 1.0f);

        // Top accent stripe
        g.setColour(accent);
        g.fillRoundedRectangle(r.getX() + 10.0f, r.getY() + 8.0f, 3.0f, 12.0f, 1.5f);

        g.setColour(juce::Colour(0xffe4edf6));
        g.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
        g.drawText(title, static_cast<int>(r.getX() + 18), static_cast<int>(r.getY() + 6),
                   static_cast<int>(r.getWidth() - 26), 16, juce::Justification::centredLeft);
    };

    drawCard({ 16.0f,  260.0f, 250.0f, 224.0f }, "PITCH TRACKER (YIN)",      juce::Colour(0xfff0c467));
    drawCard({ 278.0f, 260.0f, 210.0f, 224.0f }, "TRACKING LOW-CUT (SVF)",   juce::Colour(0xffff6e5c));
    drawCard({ 500.0f, 260.0f, 384.0f, 224.0f }, "HARMONIC SINE RECONSTRUCT", juce::Colour(0xff4ae6d8));

    // Bottom utility strip
    auto bot = juce::Rectangle<float>(16.0f, 494.0f, 868.0f, 54.0f);
    g.setColour(juce::Colour(0xff11161d));
    g.fillRoundedRectangle(bot, 6.0f);
    g.setColour(juce::Colour(0xff273240));
    g.drawRoundedRectangle(bot, 6.0f, 1.0f);

    // Parameter labels
    auto lbl = [&g](const juce::String& txt, int x, int y, int width, juce::Colour col = juce::Colour(0xff9bb0c4))
    {
        g.setColour(col);
        g.setFont(juce::Font(juce::FontOptions(9.2f, juce::Font::bold)));
        g.drawText(txt, x, y, width, 13, juce::Justification::centred);
    };

    // Card 1: Pitch Tracker
    lbl("MIN f0",      30, 300, 74);
    lbl("MAX f0",     108, 300, 74);
    lbl("GLIDE",      184, 300, 74);
    g.setColour(juce::Colour(0xff6b7e91));
    g.setFont(juce::Font(juce::FontOptions(8.8f, juce::Font::plain)));
    g.drawFittedText("Locks onto f0 in real time.\nNarrow Min/Max around your\nvocal or instrument range.",
                     28, 412, 226, 56, juce::Justification::centred, 3);

    // Card 2: Tracking Low-Cut
    lbl("SLOPE",       316, 288, 134, juce::Colour(0xffff9b8f));
    lbl("OFFSET",      292, 356, 84);
    lbl("FLOOR",       388, 356, 84);

    // Card 3: Harmonic Resynth
    lbl("MODE",        514, 288, 146, juce::Colour(0xff78ebe0));
    lbl("HARMONICS",   668, 288, 66);
    lbl("CLEAN MIX",   738, 288, 66);
    lbl("LOCK BW",     808, 288, 66);

    // Subtle divider inside Card 3 separating H1..H4 trim bank
    g.setColour(juce::Colour(0xff253140));
    g.drawHorizontalLine(386, 514.0f, 870.0f);

    lbl("H1 (FUND)",   518, 392, 82, juce::Colour(0xfff0c467));
    lbl("H2 (2x)",     608, 392, 82, juce::Colour(0xff78ebe0));
    lbl("H3 (3x)",     698, 392, 82, juce::Colour(0xff78ebe0));
    lbl("H4 (4x)",     788, 392, 82, juce::Colour(0xff78ebe0));

    // Bottom Strip labels
    lbl("AUDITION",     32, 501, 140, juce::Colour(0xff8ca4bd));
    lbl("OUT GAIN",    548, 514, 62,  juce::Colour(0xff9bb0c4));
}

void SineFairyAudioProcessorEditor::resized()
{
    // Top Real-Time Spectral Analyzer
    spectrumView.setBounds(16, 52, 868, 198);

    // Card 1: Pitch Tracker
    minF0Knob.setBounds(28,      314, 78, 86);
    maxF0Knob.setBounds(106,     314, 78, 86);
    trackSpeedKnob.setBounds(184, 314, 78, 86);

    // Card 2: Tracking Low-Cut
    slopeBox.setBounds(316, 306, 134, 26);
    offsetKnob.setBounds(292, 372, 84, 96);
    floorKnob.setBounds(388,  372, 84, 96);

    // Card 3: Harmonic Sine Reconstructor
    resynthModeBox.setBounds(514, 314, 146, 28);
    numHarmKnob.setBounds(668,    302, 66, 78);
    resynthMixKnob.setBounds(738, 302, 66, 78);
    purityBwKnob.setBounds(808,   302, 66, 78);

    h1Knob.setBounds(522, 405, 74, 74);
    h2Knob.setBounds(612, 405, 74, 74);
    h3Knob.setBounds(702, 405, 74, 74);
    h4Knob.setBounds(792, 405, 74, 74);

    // Bottom Utility Bar
    auditionBox.setBounds(32, 515, 148, 25);
    outGainKnob.setBounds(612, 498, 126, 46);
    bypassBtn.setBounds(774, 508, 96, 26);
}

#include "PluginEditor.h"

SineFairyAudioProcessorEditor::SineFairyAudioProcessorEditor(SineFairyAudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p), spectrumView(p)
{
    setLookAndFeel(&laf);
    setSize(960, 588);

    addAndMakeVisible(spectrumView);

    // Card 1: Surgical Tracking Low-Cut & Pitch Range
    slopeBox.addItemList({ "OFF", "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct", "BRICKWALL" }, 1);
    addAndMakeVisible(slopeBox);
    phaseModeBox.addItemList({ "LINEAR PHASE", "MIN PHASE" }, 1);
    addAndMakeVisible(phaseModeBox);

    setupKnob(offsetKnob, " st", 0xffff6e5c);
    setupKnob(floorKnob,  " Hz", 0xffff6e5c);
    setupKnob(minF0Knob,  " Hz", 0xfff0c467);
    setupKnob(maxF0Knob,  " Hz", 0xfff0c467);

    // Card 2: HQ Lookahead Phase-Locked 20-Harmonic Reconstructor
    resynthModeBox.addItemList({ "CLEAN (Replace)", "ENHANCE (Parallel)" }, 1);
    addAndMakeVisible(resynthModeBox);

    setupKnob(numHarmKnob,    "",   0xff4ae6d8);
    setupKnob(resynthMixKnob, " %", 0xff4ae6d8);
    setupKnob(purityKnob,     " %", 0xff4ae6d8);

    setupKnob(h1Knob,   " %", 0xfff0c467);
    setupKnob(h2Knob,   " %", 0xff5ce8dc);
    setupKnob(h34Knob,  " %", 0xff5ce8dc);
    setupKnob(h5UpKnob, " %", 0xff62b6ff);

    // Card 3: Monitor & Output
    auditionBox.addItemList({ "NORMAL", "SOLO SINES", "DELTA (NOISE)" }, 1);
    addAndMakeVisible(auditionBox);
    setupKnob(outGainKnob, " dB", 0xffb8c9dc);
    addAndMakeVisible(bypassBtn);

    auto& a = processorRef.apvts;
    attSlope       = std::make_unique<CAtt>(a, "lowCutSlope",  slopeBox);
    attPhaseMode   = std::make_unique<CAtt>(a, "lowCutPhase",  phaseModeBox);
    attOffset      = std::make_unique<SAtt>(a, "lowCutOffset", offsetKnob);
    attFloor       = std::make_unique<SAtt>(a, "lowCutFloor",  floorKnob);
    attMinF0       = std::make_unique<SAtt>(a, "minF0",        minF0Knob);
    attMaxF0       = std::make_unique<SAtt>(a, "maxF0",        maxF0Knob);

    attResynthMode = std::make_unique<CAtt>(a, "resynthMode",  resynthModeBox);
    attNumHarm     = std::make_unique<SAtt>(a, "numHarmonics", numHarmKnob);
    attResynthMix  = std::make_unique<SAtt>(a, "resynthMix",   resynthMixKnob);
    attPurity      = std::make_unique<SAtt>(a, "purityPct",    purityKnob);

    attH1          = std::make_unique<SAtt>(a, "h1Gain",       h1Knob);
    attH2          = std::make_unique<SAtt>(a, "h2Gain",       h2Knob);
    attH34         = std::make_unique<SAtt>(a, "h34Gain",      h34Knob);
    attH5Up        = std::make_unique<SAtt>(a, "h5UpGain",     h5UpKnob);

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
        phaseModeBox.getSelectedItemIndex(),
        static_cast<int>(std::round(numHarmKnob.getValue())),
        resynthModeBox.getSelectedItemIndex(),
        static_cast<float>(resynthMixKnob.getValue())
    );
}

void SineFairyAudioProcessorEditor::paint(juce::Graphics& g)
{
    const float w = static_cast<float>(getWidth());
    const float h = static_cast<float>(getHeight());

    juce::ColourGradient bg(juce::Colour(0xff191f28), 0.0f, 0.0f,
                            juce::Colour(0xff10141a), 0.0f, h, false);
    g.setGradientFill(bg);
    g.fillAll();

    // Header Bar
    g.setColour(juce::Colour(0xff0d1117));
    g.fillRect(0.0f, 0.0f, w, 44.0f);
    g.setColour(juce::Colour(0xff283444));
    g.drawHorizontalLine(44, 0.0f, w);

    g.setColour(juce::Colour(0xff678099));
    g.setFont(juce::Font(juce::FontOptions(9.8f, juce::Font::bold)));
    g.drawText("ASIMORPH's FELLAS", 20, 7, 200, 12, juce::Justification::centredLeft);

    g.setColour(juce::Colour(0xfff0f6fc));
    g.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));
    g.drawText("SINE FAIRY  v1.1", 20, 19, 210, 21, juce::Justification::centredLeft);

    g.setColour(juce::Colour(0xff52d6cb));
    g.setFont(juce::Font(juce::FontOptions(9.8f, juce::Font::bold)));
    g.drawText("HQ LOOKAHEAD 20-HARMONIC PHASE-LOCKED SINE DENOISER  +  LINEAR-PHASE LOW-CUT  |  PDC ALIGNED",
               230, 14, 710, 16, juce::Justification::centredRight);

    // Three Module Cards
    auto drawCard = [&g](juce::Rectangle<float> r, const juce::String& title, juce::Colour accent)
    {
        g.setColour(juce::Colour(0xff141a22));
        g.fillRoundedRectangle(r, 6.0f);
        g.setColour(juce::Colour(0xff2b3747));
        g.drawRoundedRectangle(r, 6.0f, 1.0f);

        g.setColour(accent);
        g.fillRoundedRectangle(r.getX() + 10.0f, r.getY() + 9.0f, 3.0f, 12.0f, 1.5f);

        g.setColour(juce::Colour(0xffe4edf6));
        g.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
        g.drawText(title, static_cast<int>(r.getX() + 18), static_cast<int>(r.getY() + 7),
                   static_cast<int>(r.getWidth() - 26), 16, juce::Justification::centredLeft);
    };

    drawCard({ 16.0f,  270.0f, 256.0f, 302.0f }, "SURGICAL LOW-CUT & f0",      juce::Colour(0xffff6e5c));
    drawCard({ 284.0f, 270.0f, 452.0f, 302.0f }, "HQ PHASE-LOCKED SINE ENGINE", juce::Colour(0xff4ae6d8));
    drawCard({ 748.0f, 270.0f, 196.0f, 302.0f }, "MONITOR & OUTPUT",           juce::Colour(0xff62b6ff));

    // Inner section dividers
    g.setColour(juce::Colour(0xff253140));
    g.drawHorizontalLine(464, 28.0f,  260.0f);
    g.drawHorizontalLine(438, 298.0f, 722.0f);

    auto lbl = [&g](const juce::String& txt, int x, int y, int width, juce::Colour col = juce::Colour(0xff9bb0c4))
    {
        g.setColour(col);
        g.setFont(juce::Font(juce::FontOptions(8.9f, juce::Font::bold)));
        g.drawText(txt, x, y, width, 13, juce::Justification::centred);
    };

    // Card 1 Labels
    lbl("SLOPE",   30, 298, 110, juce::Colour(0xffff9b8f));
    lbl("PHASE",  148, 298, 110, juce::Colour(0xffff9b8f));
    lbl("OFFSET",  38, 354,  96);
    lbl("FLOOR",  150, 354,  96);
    lbl("MIN f0",  38, 470,  96, juce::Colour(0xfff0c467));
    lbl("MAX f0", 150, 470,  96, juce::Colour(0xfff0c467));

    // Card 2 Labels
    lbl("MODE",        532, 277, 46,  juce::Colour(0xff78ebe0));
    lbl("HARMONICS",   318, 316, 116, juce::Colour(0xff78ebe0));
    lbl("CLEAN MIX",   452, 316, 116, juce::Colour(0xff78ebe0));
    lbl("PURITY GATE", 586, 316, 116, juce::Colour(0xff78ebe0));

    lbl("H1 (FUND)",   296, 446, 100, juce::Colour(0xfff0c467));
    lbl("H2 (2x)",     404, 446, 100, juce::Colour(0xff78ebe0));
    lbl("H3 - H4",     512, 446, 100, juce::Colour(0xff78ebe0));
    lbl("H5 - H20",    620, 446, 100, juce::Colour(0xff7ec4ff));

    // Card 3 Labels
    lbl("AUDITION MODE", 762, 300, 168, juce::Colour(0xff7ec4ff));
    lbl("OUTPUT GAIN",   792, 366, 108, juce::Colour(0xffb8c9dc));

    g.setColour(juce::Colour(0xff6b7e91));
    g.setFont(juce::Font(juce::FontOptions(8.6f, juce::Font::plain)));
    g.drawFittedText("4096-pt 8x WOLA Linear-Phase\nPLL-Locked Cubic SMS (H1-H20)\n5-Frame Lookahead f0 Guard\n(Zero Warble & Zero Comb)",
                     758, 472, 176, 54, juce::Justification::centred, 4);
}

void SineFairyAudioProcessorEditor::resized()
{
    spectrumView.setBounds(16, 54, 928, 206);

    // Card 1: Surgical Low-Cut & Pitch Range
    slopeBox.setBounds(30,     314, 110, 26);
    phaseModeBox.setBounds(148, 314, 110, 26);
    offsetKnob.setBounds(42,   370,  88, 86);
    floorKnob.setBounds(154,   370,  88, 86);
    minF0Knob.setBounds(42,    484,  88, 80);
    maxF0Knob.setBounds(154,   484,  88, 80);

    // Card 2: HQ Phase-Locked Sine Engine
    resynthModeBox.setBounds(580, 274, 146, 22);

    numHarmKnob.setBounds(326,    332, 100, 96);
    resynthMixKnob.setBounds(460, 332, 100, 96);
    purityKnob.setBounds(594,     332, 100, 96);

    h1Knob.setBounds(300,   462, 92, 98);
    h2Knob.setBounds(408,   462, 92, 98);
    h34Knob.setBounds(516,  462, 92, 98);
    h5UpKnob.setBounds(624, 462, 92, 98);

    // Card 3: Monitor & Output
    auditionBox.setBounds(762, 318, 168, 28);
    outGainKnob.setBounds(796, 380, 100, 88);
    bypassBtn.setBounds(784,   532, 124, 28);
}

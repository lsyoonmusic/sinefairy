#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"
#include <array>
#include <cmath>

class LookAndFeelSineFairy : public juce::LookAndFeel_V4
{
public:
    static constexpr const char* kAccentColourProp = "sfAccentCol";

    LookAndFeelSineFairy()
    {
        setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff14181e));
        setColour(juce::ComboBox::textColourId,       juce::Colour(0xffe6eef6));
        setColour(juce::ComboBox::outlineColourId,    juce::Colour(0xff334152));
        setColour(juce::ComboBox::arrowColourId,      juce::Colour(0xff5be0d8));
        setColour(juce::PopupMenu::backgroundColourId,            juce::Colour(0xff161b22));
        setColour(juce::PopupMenu::textColourId,                  juce::Colour(0xffe6eef6));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff235c66));
        setColour(juce::PopupMenu::highlightedTextColourId,       juce::Colour(0xffffffff));
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float startAngle, float endAngle,
                          juce::Slider& slider) override
    {
        const auto b = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                              static_cast<float>(width), static_cast<float>(height));
        const float cx = b.getCentreX();
        const float cy = b.getCentreY() + 1.0f;
        const float r  = juce::jmin(b.getWidth(), b.getHeight()) * 0.36f;
        const float angle = startAngle + sliderPos * (endAngle - startAngle);

        const uint32_t colHex = static_cast<uint32_t>(
            static_cast<int>(slider.getProperties().getWithDefault(kAccentColourProp, static_cast<int>(0xff4de2d8))));
        const juce::Colour accent(colHex);

        // Background track arc
        const float arcR = r + 5.5f;
        juce::Path track;
        track.addCentredArc(cx, cy, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour(juce::Colour(0xff26303d));
        g.strokePath(track, juce::PathStrokeType(2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Active value arc
        if (sliderPos > 0.002f)
        {
            juce::Path valArc;
            valArc.addCentredArc(cx, cy, arcR, arcR, 0.0f, startAngle, angle, true);
            g.setColour(accent);
            g.strokePath(valArc, juce::PathStrokeType(2.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Knob shadow & anodized body
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillEllipse(cx - r + 1.0f, cy - r + 2.5f, r * 2.0f, r * 2.0f);

        juce::ColourGradient body(juce::Colour(0xff2c3440), cx - r * 0.5f, cy - r * 0.5f,
                                  juce::Colour(0xff12161c), cx + r * 0.6f, cy + r * 0.7f, false);
        g.setGradientFill(body);
        g.fillEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);
        g.setColour(juce::Colour(0xff465263));
        g.drawEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f, 1.0f);

        // Pointer tick
        const float ptrA = angle - juce::MathConstants<float>::halfPi;
        g.setColour(accent.brighter(0.3f));
        g.drawLine(cx + std::cos(ptrA) * (r * 0.30f), cy + std::sin(ptrA) * (r * 0.30f),
                   cx + std::cos(ptrA) * (r * 0.88f), cy + std::sin(ptrA) * (r * 0.88f), 2.3f);
    }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& btn,
                          bool isHighlighted, bool) override
    {
        auto b = btn.getLocalBounds().toFloat().reduced(1.5f);
        const bool isOn = btn.getToggleState();

        g.setColour(isOn ? juce::Colour(0xff7d2b2b) : juce::Colour(0xff1b2129));
        g.fillRoundedRectangle(b, 4.0f);
        g.setColour(isHighlighted ? juce::Colour(0xff6deee6)
                                  : (isOn ? juce::Colour(0xffe05a5a) : juce::Colour(0xff384556)));
        g.drawRoundedRectangle(b, 4.0f, 1.2f);

        g.setColour(isOn ? juce::Colour(0xffffdede) : juce::Colour(0xffc9d6e5));
        g.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
        g.drawText(btn.getButtonText(), b.toNearestInt(), juce::Justification::centred);
    }
};

/**
 * Real-time Dual-Trace Log-Frequency Spectral Analyzer (20 Hz .. 4000 Hz)
 * displaying:
 *   - Dim amber-gray Input spectrum vs crisp cyan Output spectrum
 *   - Live Pitch-Tracking Butterworth Low-Cut filter transfer curve
 *   - Glowing Harmonic Lock-In pillars (H1 .. H6) + LR4 Crossover boundary
 */
class HarmonicSpectrumView : public juce::Component
{
public:
    explicit HarmonicSpectrumView(SineFairyAudioProcessor& proc)
        : processorRef(proc),
          fft(SineFairyAudioProcessor::kFftOrder)
    {
        constexpr int N = SineFairyAudioProcessor::kFftSize;
        for (int i = 0; i < N; ++i)
            hannWin[static_cast<size_t>(i)] = 0.5f * (1.0f - std::cos(6.2831853f * static_cast<float>(i) / static_cast<float>(N - 1)));

        smoothInDb.fill(-96.0f);
        smoothOutDb.fill(-96.0f);
    }

    void updateFrame(int lowCutSlopeIdx, int numHarmonics, int resynthModeIdx, float resynthMixPct)
    {
        slopeIndex   = lowCutSlopeIdx;
        activeHarms  = numHarmonics;
        modeIndex    = resynthModeIdx;
        mixFraction  = resynthMixPct * 0.01f;

        trackedF0  = processorRef.getTrackedF0();
        confidence = processorRef.getConfidence();
        lowCutHz   = processorRef.getLowCutCutoffHz();
        xoverHz    = processorRef.getXoverHz();
        sampleRate = static_cast<float>(processorRef.getCurrentSampleRate());

        for (int k = 0; k < 6; ++k)
            harmAmps[static_cast<size_t>(k)] = processorRef.getHarmonicAmp(k);

        computeSpectrum(processorRef.getInputScope(), smoothInDb);
        computeSpectrum(processorRef.getOutputScope(), smoothOutDb);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();

        // Deep dark scientific CRT/OLED display backdrop
        juce::ColourGradient bg(juce::Colour(0xff0d1218), b.getX(), b.getY(),
                                juce::Colour(0xff090c10), b.getX(), b.getBottom(), false);
        g.setGradientFill(bg);
        g.fillRoundedRectangle(b, 6.0f);
        g.setColour(juce::Colour(0xff2c3847));
        g.drawRoundedRectangle(b.reduced(0.5f), 6.0f, 1.2f);

        auto plot = b.reduced(12.0f, 10.0f);
        plot.removeFromTop(20.0f);    // Space for live telemetry badge bar
        plot.removeFromBottom(16.0f); // Space for frequency axis labels

        const float fMin = 20.0f;
        const float fMax = 4000.0f;
        const float logMin = std::log(fMin);
        const float logMax = std::log(fMax);

        auto freqToX = [&](float f) -> float
        {
            const float norm = (std::log(juce::jlimit(fMin, fMax, f)) - logMin) / (logMax - logMin);
            return plot.getX() + norm * plot.getWidth();
        };

        auto dbToY = [&](float db) -> float
        {
            const float norm = juce::jlimit(0.0f, 1.0f, (db - (-84.0f)) / (0.0f - (-84.0f)));
            return plot.getBottom() - norm * plot.getHeight();
        };

        // 1. Frequency & dB Grid Lines
        static constexpr float gridFreqs[] = { 30, 50, 80, 100, 150, 200, 300, 500, 800, 1000, 1500, 2000, 3000 };
        g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::plain)));
        for (float gf : gridFreqs)
        {
            const float gx = freqToX(gf);
            g.setColour(juce::Colour(0xff1c2530));
            g.drawVerticalLine(static_cast<int>(gx), plot.getY(), plot.getBottom());

            g.setColour(juce::Colour(0xff6b7d91));
            const juce::String lbl = (gf >= 1000.0f) ? (juce::String(gf / 1000.0f, (gf == 1500.0f) ? 1 : 0) + "k")
                                                     : juce::String(static_cast<int>(gf));
            g.drawText(lbl, static_cast<int>(gx - 16), static_cast<int>(plot.getBottom() + 2), 32, 13,
                       juce::Justification::centred);
        }

        for (float db = -72.0f; db <= -12.0f; db += 12.0f)
        {
            const float gy = dbToY(db);
            g.setColour(juce::Colour(0xff19212b));
            g.drawHorizontalLine(static_cast<int>(gy), plot.getX(), plot.getRight());
        }

        // Clip curves cleanly inside plot box
        g.saveState();
        g.reduceClipRegion(plot.toNearestInt());

        // 2. Highlight Harmonic-Cleaned Low-Band Zone (between LowCut and LR4 Crossover)
        if (confidence > 0.15f && modeIndex == 0 && mixFraction > 0.01f)
        {
            const float x1 = freqToX(lowCutHz);
            const float x2 = freqToX(xoverHz);
            if (x2 > x1)
            {
                g.setColour(juce::Colour(0xff3ce8d8).withAlpha(0.055f * confidence * mixFraction));
                g.fillRect(x1, plot.getY(), x2 - x1, plot.getHeight());
            }
        }

        // 3. Draw Dual-Trace FFT Spectra (Input amber-gray fill + Output bright cyan trace)
        auto buildSpecPath = [&](const std::array<float, kBins>& dbArr, bool closeFill) -> juce::Path
        {
            juce::Path path;
            const int numPts = 220;
            for (int i = 0; i <= numPts; ++i)
            {
                const float t = static_cast<float>(i) / static_cast<float>(numPts);
                const float f = std::exp(logMin + t * (logMax - logMin));
                const float binF = f * static_cast<float>(SineFairyAudioProcessor::kFftSize) / std::max(sampleRate, 8000.0f);
                const int b0 = juce::jlimit(1, kBins - 2, static_cast<int>(binF));
                const float frac = binF - static_cast<float>(b0);
                const float db = dbArr[static_cast<size_t>(b0)] * (1.0f - frac)
                               + dbArr[static_cast<size_t>(b0 + 1)] * frac;

                const float px = plot.getX() + t * plot.getWidth();
                const float py = dbToY(db);
                if (i == 0) path.startNewSubPath(px, py);
                else        path.lineTo(px, py);
            }
            if (closeFill)
            {
                path.lineTo(plot.getRight(), plot.getBottom());
                path.lineTo(plot.getX(), plot.getBottom());
                path.closeSubPath();
            }
            return path;
        };

        // Input spectrum (subtle warm amber shadow -> makes carved noise valleys immediately visible!)
        auto inFill = buildSpecPath(smoothInDb, true);
        g.setColour(juce::Colour(0xffd99757).withAlpha(0.14f));
        g.fillPath(inFill);
        auto inStroke = buildSpecPath(smoothInDb, false);
        g.setColour(juce::Colour(0xffd99757).withAlpha(0.45f));
        g.strokePath(inStroke, juce::PathStrokeType(1.1f));

        // Output spectrum (crisp cyan-mint glow)
        auto outFill = buildSpecPath(smoothOutDb, true);
        g.setColour(juce::Colour(0xff3ce8d8).withAlpha(0.16f));
        g.fillPath(outFill);
        auto outStroke = buildSpecPath(smoothOutDb, false);
        g.setColour(juce::Colour(0xff52f2e4));
        g.strokePath(outStroke, juce::PathStrokeType(1.6f));

        // 4. Harmonic Lock-In Pillars (H1 .. H_N)
        if (confidence > 0.10f)
        {
            g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
            for (int k = 0; k < activeHarms; ++k)
            {
                const float hkFreq = trackedF0 * static_cast<float>(k + 1);
                if (hkFreq < fMin || hkFreq > fMax)
                    continue;

                const float hx = freqToX(hkFreq);
                const float ampDb = 20.0f * std::log10(std::max(harmAmps[static_cast<size_t>(k)], 1e-5f));
                const float hy = dbToY(ampDb);

                const juce::Colour pCol = (k == 0) ? juce::Colour(0xffffcf66) : juce::Colour(0xff6bf5e8);
                g.setColour(pCol.withAlpha(0.28f * confidence));
                g.drawVerticalLine(static_cast<int>(hx), plot.getY() + 14.0f, plot.getBottom());

                // Live extracted sine energy pillar
                g.setColour(pCol.withAlpha(0.75f * confidence));
                g.fillRect(hx - 2.0f, hy, 4.0f, std::max(0.0f, plot.getBottom() - hy));

                // Harmonic badge at top
                g.setColour(pCol.withAlpha(0.92f * confidence));
                g.drawText("H" + juce::String(k + 1),
                           static_cast<int>(hx - 12), static_cast<int>(plot.getY() + 1), 24, 12,
                           juce::Justification::centred);
            }

            // LR4 Crossover dashed boundary marker
            if (xoverHz >= fMin && xoverHz <= fMax)
            {
                const float xx = freqToX(xoverHz);
                g.setColour(juce::Colour(0xff9bb4cc).withAlpha(0.55f));
                const float dashes[2] = { 4.0f, 4.0f };
                g.drawDashedLine(juce::Line<float>(xx, plot.getY() + 14.0f, xx, plot.getBottom()),
                                 dashes, 2, 1.1f);
                g.setFont(juce::Font(juce::FontOptions(8.2f, juce::Font::bold)));
                g.drawText("XOVER", static_cast<int>(xx - 18), static_cast<int>(plot.getY() + 2), 36, 11,
                           juce::Justification::centred);
            }
        }

        // 5. Live Pitch-Tracking Low-Cut Filter Curve (Butterworth magnitude response)
        if (slopeIndex > 0)
        {
            const int order = slopeIndex * 2; // 2, 4, 6, 8
            juce::Path lcCurve;
            const int pts = 180;
            for (int i = 0; i <= pts; ++i)
            {
                const float t = static_cast<float>(i) / static_cast<float>(pts);
                const float f = std::exp(logMin + t * (logMax - logMin));
                const float ratio = lowCutHz / std::max(f, 1.0f);
                const float magSq = 1.0f / (1.0f + std::pow(ratio, static_cast<float>(2 * order)));
                const float db = 10.0f * std::log10(std::max(magSq, 1e-9f));
                // Place 0 dB filter passband line near -6 dB height so curve reads super clearly
                const float py = dbToY(-6.0f + db);
                const float px = plot.getX() + t * plot.getWidth();
                if (i == 0) lcCurve.startNewSubPath(px, py);
                else        lcCurve.lineTo(px, py);
            }
            g.setColour(juce::Colour(0xffff6e5c));
            g.strokePath(lcCurve, juce::PathStrokeType(2.2f));

            // Glowing cutoff dot on the -3 dB knee
            const float cx = freqToX(lowCutHz);
            const float cy = dbToY(-9.0f);
            g.setColour(juce::Colour(0xffff6e5c));
            g.fillEllipse(cx - 4.0f, cy - 4.0f, 8.0f, 8.0f);
        }

        g.restoreState();

        // 6. Top Telemetry Header Strip inside analyzer
        const auto topStrip = juce::Rectangle<float>(b.getX() + 14.0f, b.getY() + 7.0f, b.getWidth() - 28.0f, 18.0f);
        const bool locked = (confidence > 0.30f);

        // Pitch lock LED
        g.setColour(locked ? juce::Colour(0xff42f5c5) : juce::Colour(0xff424c59));
        g.fillEllipse(topStrip.getX(), topStrip.getCentreY() - 4.0f, 8.0f, 8.0f);

        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.setColour(locked ? juce::Colour(0xffffdf8a) : juce::Colour(0xff7d8ea3));
        const juce::String noteStr = locked
            ? ("f0 LOCK: " + freqToNoteName(trackedF0) + "  (" + juce::String(trackedF0, 1) + " Hz)")
            : "f0: UNVOICED / IDLE";
        g.drawText(noteStr, static_cast<int>(topStrip.getX() + 14), static_cast<int>(topStrip.getY()),
                   240, 18, juce::Justification::centredLeft);

        g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        g.setColour(juce::Colour(0xffff8a7a));
        g.drawText("LOW-CUT: " + juce::String(lowCutHz, 1) + " Hz",
                   static_cast<int>(topStrip.getX() + 265), static_cast<int>(topStrip.getY()),
                   150, 18, juce::Justification::centredLeft);

        g.setColour(juce::Colour(0xff78e8de));
        g.drawText("X-OVER: " + juce::String(xoverHz, 0) + " Hz   (H1-H" + juce::String(activeHarms) + ")",
                   static_cast<int>(topStrip.getX() + 425), static_cast<int>(topStrip.getY()),
                   180, 18, juce::Justification::centredLeft);

        // Legend on right
        g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
        g.setColour(juce::Colour(0xffd99757));
        g.drawText("IN", static_cast<int>(topStrip.getRight() - 78), static_cast<int>(topStrip.getY()),
                   34, 18, juce::Justification::centredRight);
        g.setColour(juce::Colour(0xff52f2e4));
        g.drawText("OUT", static_cast<int>(topStrip.getRight() - 38), static_cast<int>(topStrip.getY()),
                   38, 18, juce::Justification::centredRight);
    }

private:
    static constexpr int kBins = SineFairyAudioProcessor::kFftSize / 2;

    void computeSpectrum(const LockFreeAudioScope<SineFairyAudioProcessor::kScopeCapacity>& scope,
                         std::array<float, kBins>& targetDb)
    {
        constexpr int N = SineFairyAudioProcessor::kFftSize;
        fftWork.fill(0.0f);
        scope.copyLatest(fftWork.data(), N);

        for (int i = 0; i < N; ++i)
            fftWork[static_cast<size_t>(i)] *= hannWin[static_cast<size_t>(i)];

        fft.performFrequencyOnlyForwardTransform(fftWork.data());

        const float norm = 2.0f / static_cast<float>(N);
        for (int b = 0; b < kBins; ++b)
        {
            const float mag = fftWork[static_cast<size_t>(b)] * norm;
            const float db  = juce::jlimit(-96.0f, 0.0f, 20.0f * std::log10(std::max(mag, 1e-5f)));
            const float cur = targetDb[static_cast<size_t>(b)];
            const float alpha = (db > cur) ? 0.55f : 0.22f;
            targetDb[static_cast<size_t>(b)] = cur + alpha * (db - cur);
        }
    }

    static juce::String freqToNoteName(float hz)
    {
        static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        if (hz < 16.0f) return "---";
        const int midi = static_cast<int>(std::round(69.0 + 12.0 * std::log2(hz / 440.0)));
        const int note = ((midi % 12) + 12) % 12;
        const int oct  = (midi / 12) - 1;
        return juce::String(names[note]) + juce::String(oct);
    }

    SineFairyAudioProcessor& processorRef;
    juce::dsp::FFT fft;
    std::array<float, SineFairyAudioProcessor::kFftSize> hannWin {};
    std::array<float, SineFairyAudioProcessor::kFftSize * 2> fftWork {};
    std::array<float, kBins> smoothInDb {};
    std::array<float, kBins> smoothOutDb {};

    float sampleRate = 48000.0f;
    float trackedF0  = 150.0f;
    float confidence = 0.0f;
    float lowCutHz   = 65.0f;
    float xoverHz    = 650.0f;
    int   slopeIndex = 2;
    int   activeHarms = 4;
    int   modeIndex  = 0;
    float mixFraction = 0.65f;
    std::array<float, 6> harmAmps {};
};

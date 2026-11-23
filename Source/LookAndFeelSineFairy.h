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
        const float r  = juce::jmin(b.getWidth(), b.getHeight()) * 0.35f;
        const float angle = startAngle + sliderPos * (endAngle - startAngle);

        const uint32_t colHex = static_cast<uint32_t>(
            static_cast<int>(slider.getProperties().getWithDefault(kAccentColourProp, static_cast<int>(0xff4de2d8))));
        const juce::Colour accent(colHex);

        const float arcR = r + 5.0f;
        juce::Path track;
        track.addCentredArc(cx, cy, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour(juce::Colour(0xff26303d));
        g.strokePath(track, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (sliderPos > 0.002f)
        {
            juce::Path valArc;
            valArc.addCentredArc(cx, cy, arcR, arcR, 0.0f, startAngle, angle, true);
            g.setColour(accent);
            g.strokePath(valArc, juce::PathStrokeType(2.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillEllipse(cx - r + 1.0f, cy - r + 2.5f, r * 2.0f, r * 2.0f);

        juce::ColourGradient body(juce::Colour(0xff2c3440), cx - r * 0.5f, cy - r * 0.5f,
                                  juce::Colour(0xff12161c), cx + r * 0.6f, cy + r * 0.7f, false);
        g.setGradientFill(body);
        g.fillEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);
        g.setColour(juce::Colour(0xff465263));
        g.drawEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f, 1.0f);

        const float ptrA = angle - juce::MathConstants<float>::halfPi;
        g.setColour(accent.brighter(0.3f));
        g.drawLine(cx + std::cos(ptrA) * (r * 0.28f), cy + std::sin(ptrA) * (r * 0.28f),
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

    void updateFrame(int lowCutSlopeIdx, int phaseModeIdx, int numHarmonics, int resynthModeIdx, float resynthMixPct)
    {
        slopeIndex   = lowCutSlopeIdx;
        phaseMode    = phaseModeIdx;
        activeHarms  = numHarmonics;
        modeIndex    = resynthModeIdx;
        mixFraction  = resynthMixPct * 0.01f;

        trackedF0  = processorRef.getTrackedF0();
        confidence = processorRef.getConfidence();
        lowCutHz   = processorRef.getLowCutCutoffHz();
        xoverHz    = processorRef.getXoverHz();
        sampleRate = static_cast<float>(processorRef.getCurrentSampleRate());
        latencyMs  = 1000.0f * static_cast<float>(processorRef.getEngineLatencySamples()) / std::max(sampleRate, 8000.0f);

        for (int k = 0; k < SineFairyAudioProcessor::kMaxHarmonics; ++k)
            harmAmps[static_cast<size_t>(k)] = processorRef.getHarmonicAmp(k);

        computeSpectrum(processorRef.getInputScope(), smoothInDb);
        computeSpectrum(processorRef.getOutputScope(), smoothOutDb);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();

        juce::ColourGradient bg(juce::Colour(0xff0d1218), b.getX(), b.getY(),
                                juce::Colour(0xff090c10), b.getX(), b.getBottom(), false);
        g.setGradientFill(bg);
        g.fillRoundedRectangle(b, 6.0f);
        g.setColour(juce::Colour(0xff2c3847));
        g.drawRoundedRectangle(b.reduced(0.5f), 6.0f, 1.2f);

        auto plot = b.reduced(12.0f, 10.0f);
        plot.removeFromTop(21.0f);
        plot.removeFromBottom(16.0f);

        const float fMin = 20.0f;
        const float fMax = 16000.0f;
        const float logMin = std::log(fMin);
        const float logMax = std::log(fMax);

        auto freqToX = [&](float f) -> float
        {
            const float norm = (std::log(juce::jlimit(fMin, fMax, f)) - logMin) / (logMax - logMin);
            return plot.getX() + norm * plot.getWidth();
        };

        auto dbToY = [&](float db) -> float
        {
            const float norm = juce::jlimit(0.0f, 1.0f, (db - (-84.0f)) / 84.0f);
            return plot.getBottom() - norm * plot.getHeight();
        };

        // 1. Frequency & dB Grid (20 Hz .. 16 kHz to showcase up to H20!)
        static constexpr float gridFreqs[] = { 30, 50, 80, 100, 200, 300, 500, 1000, 2000, 4000, 8000, 12000 };
        g.setFont(juce::Font(juce::FontOptions(8.8f, juce::Font::plain)));
        for (float gf : gridFreqs)
        {
            const float gx = freqToX(gf);
            g.setColour(juce::Colour(0xff1c2530));
            g.drawVerticalLine(static_cast<int>(gx), plot.getY(), plot.getBottom());

            g.setColour(juce::Colour(0xff6b7d91));
            const juce::String lbl = (gf >= 1000.0f) ? (juce::String(static_cast<int>(gf / 1000.0f)) + "k")
                                                     : juce::String(static_cast<int>(gf));
            g.drawText(lbl, static_cast<int>(gx - 15), static_cast<int>(plot.getBottom() + 2), 30, 13,
                       juce::Justification::centred);
        }

        for (float db = -72.0f; db <= -12.0f; db += 12.0f)
        {
            const float gy = dbToY(db);
            g.setColour(juce::Colour(0xff19212b));
            g.drawHorizontalLine(static_cast<int>(gy), plot.getX(), plot.getRight());
        }

        g.saveState();
        g.reduceClipRegion(plot.toNearestInt());

        // 2. Highlight Cleaned Harmonic Zone (between LowCut and Dynamic XOVER)
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

        // 3. Dual-Trace FFT Spectra (Input amber + Output crisp mint-cyan)
        auto buildSpecPath = [&](const std::array<float, kBins>& dbArr, bool closeFill) -> juce::Path
        {
            juce::Path path;
            const int numPts = 260;
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

        auto inFill = buildSpecPath(smoothInDb, true);
        g.setColour(juce::Colour(0xffd99757).withAlpha(0.14f));
        g.fillPath(inFill);
        auto inStroke = buildSpecPath(smoothInDb, false);
        g.setColour(juce::Colour(0xffd99757).withAlpha(0.45f));
        g.strokePath(inStroke, juce::PathStrokeType(1.1f));

        auto outFill = buildSpecPath(smoothOutDb, true);
        g.setColour(juce::Colour(0xff3ce8d8).withAlpha(0.16f));
        g.fillPath(outFill);
        auto outStroke = buildSpecPath(smoothOutDb, false);
        g.setColour(juce::Colour(0xff52f2e4));
        g.strokePath(outStroke, juce::PathStrokeType(1.6f));

        // 4. Phase-Locked Reconstructed Harmonic Pillars (H1 .. H20)
        if (confidence > 0.10f)
        {
            g.setFont(juce::Font(juce::FontOptions(8.2f, juce::Font::bold)));
            for (int k = 0; k < activeHarms; ++k)
            {
                const float hkFreq = trackedF0 * static_cast<float>(k + 1);
                if (hkFreq < fMin || hkFreq > fMax)
                    continue;

                const float hx = freqToX(hkFreq);
                const float ampDb = 20.0f * std::log10(std::max(harmAmps[static_cast<size_t>(k)], 1e-5f));
                const float hy = dbToY(ampDb);

                const juce::Colour pCol = (k == 0)
                    ? juce::Colour(0xffffcf66)
                    : juce::Colour(0xff5cf2e4).interpolatedWith(juce::Colour(0xff62b6ff), static_cast<float>(k) / 19.0f);

                const float barW = (k < 4) ? 3.2f : 2.0f;
                g.setColour(pCol.withAlpha(0.76f * confidence));
                g.fillRect(hx - barW * 0.5f, hy, barW, std::max(0.0f, plot.getBottom() - hy));

                if (k <= 3 || k == 5 || k == 9 || k == 14 || k == activeHarms - 1)
                {
                    g.setColour(pCol.withAlpha(0.92f * confidence));
                    g.drawText("H" + juce::String(k + 1),
                               static_cast<int>(hx - 12), static_cast<int>(plot.getY() + 1), 24, 11,
                               juce::Justification::centred);
                }
            }

            // Dynamic Linear-Phase Crossover Boundary
            if (xoverHz >= fMin && xoverHz <= fMax)
            {
                const float xx = freqToX(xoverHz);
                g.setColour(juce::Colour(0xff9bb4cc).withAlpha(0.60f));
                const float dashes[2] = { 4.0f, 4.0f };
                g.drawDashedLine(juce::Line<float>(xx, plot.getY() + 13.0f, xx, plot.getBottom()),
                                 dashes, 2, 1.1f);
                g.setFont(juce::Font(juce::FontOptions(8.2f, juce::Font::bold)));
                g.drawText("XOVER", static_cast<int>(xx + 3), static_cast<int>(plot.getY() + 14), 38, 11,
                           juce::Justification::centredLeft);
            }
        }

        // 5. Surgical Pitch-Tracking Low-Cut Curve (up to 96 dB/oct + BRICKWALL!)
        if (slopeIndex > 0)
        {
            const bool isBrick = (slopeIndex == 7);
            static constexpr int orders[6] = { 2, 4, 6, 8, 12, 16 };
            const int order = orders[juce::jlimit(0, 5, slopeIndex - 1)];

            juce::Path lcCurve;
            const int pts = 240;
            for (int i = 0; i <= pts; ++i)
            {
                const float t = static_cast<float>(i) / static_cast<float>(pts);
                const float f = std::exp(logMin + t * (logMax - logMin));
                float db = 0.0f;

                if (isBrick)
                {
                    const float trans = std::max(18.0f, lowCutHz * 0.06f);
                    const float u = juce::jlimit(0.0f, 1.0f, (f - (lowCutHz - trans)) / (2.0f * trans));
                    const float mag = 0.5f - 0.5f * std::cos(3.14159265f * u);
                    db = 20.0f * std::log10(std::max(mag, 1e-4f));
                }
                else
                {
                    const float ratio = lowCutHz / std::max(f, 1.0f);
                    const float logRatio = std::log10(std::max(ratio, 1e-4f));
                    db = (ratio <= 1.0f)
                        ? (-10.0f * std::log10(1.0f + std::pow(ratio, static_cast<float>(2 * order))))
                        : (-10.0f * std::log10(1.0f + std::pow(10.0f, std::min(12.0f, static_cast<float>(2 * order) * logRatio))));
                }

                const float py = dbToY(-6.0f + db);
                const float px = plot.getX() + t * plot.getWidth();
                if (i == 0) lcCurve.startNewSubPath(px, py);
                else        lcCurve.lineTo(px, py);
            }
            g.setColour(juce::Colour(0xffff6e5c));
            g.strokePath(lcCurve, juce::PathStrokeType(2.2f));

            const float cx = freqToX(lowCutHz);
            const float cy = dbToY(-9.0f);
            g.setColour(juce::Colour(0xffff6e5c));
            g.fillEllipse(cx - 4.0f, cy - 4.0f, 8.0f, 8.0f);
        }

        g.restoreState();

        // 6. Top Telemetry Header Strip
        const auto topStrip = juce::Rectangle<float>(b.getX() + 14.0f, b.getY() + 7.0f, b.getWidth() - 28.0f, 18.0f);
        const bool locked = (confidence > 0.25f);

        g.setColour(locked ? juce::Colour(0xff42f5c5) : juce::Colour(0xff424c59));
        g.fillEllipse(topStrip.getX(), topStrip.getCentreY() - 4.0f, 8.0f, 8.0f);

        g.setFont(juce::Font(juce::FontOptions(10.8f, juce::Font::bold)));
        g.setColour(locked ? juce::Colour(0xffffdf8a) : juce::Colour(0xff7d8ea3));
        const juce::String noteStr = locked
            ? ("f0 LOCK: " + freqToNoteName(trackedF0) + " (" + juce::String(trackedF0, 1) + " Hz)")
            : "f0: UNVOICED / IDLE";
        g.drawText(noteStr, static_cast<int>(topStrip.getX() + 14), static_cast<int>(topStrip.getY()),
                   215, 18, juce::Justification::centredLeft);

        static constexpr const char* slopeNames[8] = { "OFF", "12dB", "24dB", "36dB", "48dB", "72dB", "96dB", "BRICK" };
        const juce::String phStr = (phaseMode == 0) ? "LIN" : "MIN";
        g.setFont(juce::Font(juce::FontOptions(9.8f, juce::Font::bold)));
        g.setColour(juce::Colour(0xffff8a7a));
        g.drawText("LOW-CUT: " + juce::String(lowCutHz, 1) + " Hz (" + slopeNames[juce::jlimit(0, 7, slopeIndex)] + " " + phStr + ")",
                   static_cast<int>(topStrip.getX() + 235), static_cast<int>(topStrip.getY()),
                   215, 18, juce::Justification::centredLeft);

        g.setColour(juce::Colour(0xff78e8de));
        g.drawText("H1-H" + juce::String(activeHarms) + " LOCK  |  XOVER " + juce::String(xoverHz, 0) + " Hz",
                   static_cast<int>(topStrip.getX() + 460), static_cast<int>(topStrip.getY()),
                   210, 18, juce::Justification::centredLeft);

        g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
        g.setColour(juce::Colour(0xff8498ad));
        g.drawText("LOOKAHEAD " + juce::String(latencyMs, 1) + " ms",
                   static_cast<int>(topStrip.getRight() - 185), static_cast<int>(topStrip.getY()),
                   112, 18, juce::Justification::centredRight);

        g.setColour(juce::Colour(0xffd99757));
        g.drawText("IN", static_cast<int>(topStrip.getRight() - 68), static_cast<int>(topStrip.getY()),
                   30, 18, juce::Justification::centredRight);
        g.setColour(juce::Colour(0xff52f2e4));
        g.drawText("OUT", static_cast<int>(topStrip.getRight() - 34), static_cast<int>(topStrip.getY()),
                   34, 18, juce::Justification::centredRight);
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

    float sampleRate  = 48000.0f;
    float latencyMs   = 85.3f;
    float trackedF0   = 180.0f;
    float confidence  = 0.0f;
    float lowCutHz    = 65.0f;
    float xoverHz     = 1800.0f;
    int   slopeIndex  = 3;
    int   phaseMode   = 0;
    int   activeHarms = 8;
    int   modeIndex   = 0;
    float mixFraction = 0.65f;
    std::array<float, SineFairyAudioProcessor::kMaxHarmonics> harmAmps {};
};

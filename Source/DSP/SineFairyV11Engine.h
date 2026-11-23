#pragma once
#include "FftRadix2.h"
#include "TptSvf.h"
#include "HarmonicFrameAnalyzer.h"
#include "PhaseLockedSmsSynth.h"
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>

namespace sinefairy
{

enum class AuditionMode : int
{
    Normal    = 0,
    SoloSines = 1, // Reconstructed phase-locked pure harmonic sines ONLY
    Delta     = 2  // Exact removed sub-f0 rumble + inter-harmonic mud ONLY
};

struct EngineParams
{
    // Pitch Tracker Range
    double minF0Hz = 75.0;
    double maxF0Hz = 680.0;

    // Surgical Pitch-Tracking Low-Cut
    LowCutSlope     lowCutSlope    = LowCutSlope::dB36;
    FilterPhaseMode lowCutPhase    = FilterPhaseMode::LinearPhase;
    double          lowCutOffsetSt = -3.5;  // -12 .. 0 st below f0
    double          lowCutFloorHz  = 65.0;  // Resting cutoff when unvoiced

    // Phase-Locked 20-Harmonic Sine Reconstructor
    ResynthMode resynthMode = ResynthMode::CleanReplace;
    int    numHarmonics     = 8;     // 1 .. 20 harmonics!
    double resynthMix       = 0.65;  // 0 .. 1
    double purity01         = 0.55;  // 0 .. 1 (noise floor subtraction & prominence gate)
    double gH1              = 1.0;   // 0 .. 2 (1st harmonic trim)
    double gH2              = 1.0;   // 0 .. 2 (2nd harmonic trim)
    double gH34             = 1.0;   // 0 .. 2 (3rd-4th harmonic trim)
    double gH5Up            = 1.0;   // 0 .. 2 (5th-20th harmonic trim)

    // Output & Audition
    AuditionMode audition = AuditionMode::Normal;
    double outputGainDb   = 0.0;
    bool   bypass         = false;
};

/**
 * Sine Fairy v1.1 (HQ Lookahead Edition) Core Stereo Engine.
 *
 * Total Engine Latency = EXACTLY N samples (4096 @ 44.1/48 kHz = 85.3 ms; 8192 @ 96 kHz = 85.3 ms).
 * Exact 0.000-sample alignment between Linear-Phase WOLA Low-Cut/Crossover and McAulay-Quatieri
 * Cubic-Phase-Locked 20-Harmonic Sinusoidal Reconstruction!
 */
class SineFairyV11Engine
{
public:
    static constexpr int kQueueSize = 16;

    void prepare(double sampleRate)
    {
        fs = std::max(sampleRate, 8000.0);
        N = (fs > 64000.0) ? 8192 : 4096;
        H = N / 8;
        numBins = N / 2 + 1;
        olaNorm = (2.0 * static_cast<double>(H)) / static_cast<double>(N);

        fft.prepare(N);
        analyzer.prepare(fs, N);
        smsSynth.prepare(fs, H);
        minPhaseLowCut.prepare(fs);

        sqrtHann.resize(static_cast<size_t>(N));
        constexpr double twoPi = 6.2831853071795864769;
        for (int i = 0; i < N; ++i)
        {
            const double hann = 0.5 * (1.0 - std::cos(twoPi * static_cast<double>(i) / static_cast<double>(N)));
            sqrtHann[static_cast<size_t>(i)] = std::sqrt(std::max(0.0, hann));
        }

        for (int ch = 0; ch < 2; ++ch)
        {
            inRing[ch].assign(static_cast<size_t>(N), 0.0);
            rawDelayRing[ch].assign(static_cast<size_t>(N), 0.0);
            olaDryRing[ch].assign(static_cast<size_t>(N), 0.0);
            olaHighRing[ch].assign(static_cast<size_t>(N), 0.0);
            wolaRe[ch].assign(static_cast<size_t>(N), 0.0);
            wolaIm[ch].assign(static_cast<size_t>(N), 0.0);
            hannRe[ch].assign(static_cast<size_t>(N), 0.0);
            hannIm[ch].assign(static_cast<size_t>(N), 0.0);
        }

        monoTimeScratch.assign(static_cast<size_t>(N), 0.0);
        fftScratchRe.assign(static_cast<size_t>(N), 0.0);
        fftScratchIm.assign(static_cast<size_t>(N), 0.0);
        maskLc.assign(static_cast<size_t>(numBins), 1.0);
        maskHigh.assign(static_cast<size_t>(numBins), 0.0);
        maskHighTotal.assign(static_cast<size_t>(numBins), 0.0);

        minPhaseCutAlpha = 1.0 - std::exp(-1.0 / (0.005 * fs));
        confSmoothAlpha  = 1.0 - std::exp(-1.0 / (0.008 * fs));

        reset();
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill(inRing[ch].begin(),       inRing[ch].end(),       0.0);
            std::fill(rawDelayRing[ch].begin(), rawDelayRing[ch].end(), 0.0);
            std::fill(olaDryRing[ch].begin(),   olaDryRing[ch].end(),   0.0);
            std::fill(olaHighRing[ch].begin(),  olaHighRing[ch].end(),  0.0);
        }

        inPos = 0;
        outPos = 0;
        hopCounter = 0;
        frameWriteIdx = 0;
        for (auto& f : frameQueue)
            f = HarmonicFrame {};

        smsSynth.reset();
        minPhaseLowCut.reset();

        smoothLinearLcHz   = 65.0;
        smoothLinearXovHz  = 1800.0;
        smoothMinPhaseLcHz = 65.0;
        smoothedPlayConf   = 0.0;

        lastValidF0  = 180.0;
        liveF0       = 180.0;
        liveConf     = 0.0;
        liveLowCutHz = 65.0;
        liveXoverHz  = 1800.0;
        liveHarmAmps.fill(0.0);
    }

    int getLatencySamples() const noexcept { return N; }

    inline void processStereoSample(double& left, double& right, const EngineParams& p) noexcept
    {
        // 1. Read exact N-sample latency-aligned raw & WOLA outputs
        const double delayedRawL = rawDelayRing[0][static_cast<size_t>(inPos)];
        const double delayedRawR = rawDelayRing[1][static_cast<size_t>(inPos)];

        const double wolaDryL  = olaDryRing[0][static_cast<size_t>(outPos)];
        const double wolaDryR  = olaDryRing[1][static_cast<size_t>(outPos)];
        const double wolaHighL = olaHighRing[0][static_cast<size_t>(outPos)];
        const double wolaHighR = olaHighRing[1][static_cast<size_t>(outPos)];

        olaDryRing[0][static_cast<size_t>(outPos)]  = 0.0;
        olaDryRing[1][static_cast<size_t>(outPos)]  = 0.0;
        olaHighRing[0][static_cast<size_t>(outPos)] = 0.0;
        olaHighRing[1][static_cast<size_t>(outPos)] = 0.0;

        // Exact complementary Linear-Phase Low-Band (zero extra IFFT!):
        //   wolaDry = IFFT(X * maskLc),  wolaHigh = IFFT(X * maskLc * maskHigh)
        //   => wolaLow = wolaDry - wolaHigh = IFFT(X * maskLc * (1 - maskHigh))
        const double wolaLowL = wolaDryL - wolaHighL;
        const double wolaLowR = wolaDryR - wolaHighR;

        // 2. Synthesize 1 sample of McAulay-Quatieri Cubic-Phase-Locked Sines BEFORE advancing hop!
        //    This achieves EXACT 0.000-sample phase lock with wolaDry / wolaLow!
        const auto sOut = smsSynth.processSample(p.gH1, p.gH2, p.gH34, p.gH5Up);

        // 3. Push incoming sample into rings & advance pointers
        inRing[0][static_cast<size_t>(inPos)]       = left;
        inRing[1][static_cast<size_t>(inPos)]       = right;
        rawDelayRing[0][static_cast<size_t>(inPos)] = left;
        rawDelayRing[1][static_cast<size_t>(inPos)] = right;

        inPos  = (inPos  + 1) & (N - 1);
        outPos = (outPos + 1) & (N - 1);

        // 4. Every H samples, run 4096/8192-pt WOLA + zero-phase stereo analysis + lookahead dispatch
        if (++hopCounter >= H)
        {
            hopCounter = 0;
            processHop(p);
        }

        // Smooth voicing confidence gate for artifact-free unvoiced consonant protection
        smoothedPlayConf += confSmoothAlpha * (liveConf - smoothedPlayConf);

        // True latency-compensated bypass (zero timing shift when toggling A/B!)
        if (p.bypass)
        {
            left  = delayedRawL;
            right = delayedRawR;
            return;
        }

        // 5. Select Linear-Phase or Lookahead-Guided Min-Phase Low-Cut
        const double logCurMp = std::log(std::max(smoothMinPhaseLcHz, 15.0));
        const double logTgtMp = std::log(std::max(liveLowCutHz, 15.0));
        smoothMinPhaseLcHz = std::exp(logCurMp + minPhaseCutAlpha * (logTgtMp - logCurMp));

        double cleanDryL = wolaDryL;
        double cleanDryR = wolaDryR;
        if (p.lowCutPhase == FilterPhaseMode::MinPhase)
        {
            cleanDryL = delayedRawL;
            cleanDryR = delayedRawR;
            minPhaseLowCut.processStereoSample(cleanDryL, cleanDryR, p.lowCutSlope, smoothMinPhaseLcHz);
        }

        const double removedSubL = delayedRawL - cleanDryL;
        const double removedSubR = delayedRawR - cleanDryR;

        // 6. Mix Phase-Locked Harmonic Sines with Original Audio
        const double effMix = std::clamp(p.resynthMix, 0.0, 1.0) * std::clamp(smoothedPlayConf, 0.0, 1.0);

        double outL = 0.0, outR = 0.0;
        if (p.audition == AuditionMode::SoloSines)
        {
            outL = sOut.shapedFullL;
            outR = sOut.shapedFullR;
        }
        else if (p.audition == AuditionMode::Delta)
        {
            if (p.resynthMode == ResynthMode::CleanReplace)
            {
                // Exact removed sub-f0 rumble + exact removed inter-harmonic noise inside low-band!
                outL = removedSubL + effMix * (wolaLowL - sOut.unityCleanL);
                outR = removedSubR + effMix * (wolaLowR - sOut.unityCleanR);
            }
            else
            {
                outL = removedSubL;
                outR = removedSubR;
            }
        }
        else // Normal
        {
            if (p.resynthMode == ResynthMode::CleanReplace)
            {
                // Replace noisy low-band below (N+0.5)*f0 with phase-locked pure sines!
                // If MinPhase LowCut is selected, apply the low-band cleaning delta onto cleanDry
                if (p.lowCutPhase == FilterPhaseMode::LinearPhase)
                {
                    outL = wolaHighL + (1.0 - effMix) * wolaLowL + effMix * sOut.shapedCleanL;
                    outR = wolaHighR + (1.0 - effMix) * wolaLowR + effMix * sOut.shapedCleanR;
                }
                else
                {
                    outL = cleanDryL + effMix * (sOut.shapedCleanL - wolaLowL);
                    outR = cleanDryR + effMix * (sOut.shapedCleanR - wolaLowR);
                }
            }
            else // EnhanceAdd (Parallel)
            {
                outL = cleanDryL + effMix * sOut.shapedFullL;
                outR = cleanDryR + effMix * sOut.shapedFullR;
            }
        }

        const double outLin = std::pow(10.0, p.outputGainDb * 0.05);
        left  = outL * outLin;
        right = outR * outLin;
    }

    double getTrackedF0() const noexcept        { return liveF0; }
    double getConfidence() const noexcept       { return liveConf; }
    double getLowCutCutoffHz() const noexcept   { return liveLowCutHz; }
    double getXoverHz() const noexcept          { return liveXoverHz; }
    double getHarmonicAmp(int k) const noexcept
    {
        return liveHarmAmps[static_cast<size_t>(std::clamp(k, 0, kMaxHarmonics - 1))];
    }

private:
    void processHop(const EngineParams& p) noexcept
    {
        const int halfN = N / 2;

        // A. Pack Stereo sqrt-Hann WOLA input -> 1 complex FFT!
        for (int i = 0; i < N; ++i)
        {
            const int rIdx = (inPos + i) & (N - 1);
            const double w = sqrtHann[static_cast<size_t>(i)];
            const double l = inRing[0][static_cast<size_t>(rIdx)];
            const double r = inRing[1][static_cast<size_t>(rIdx)];

            fftScratchRe[static_cast<size_t>(i)] = l * w;
            fftScratchIm[static_cast<size_t>(i)] = r * w;
            monoTimeScratch[static_cast<size_t>(i)] = 0.5 * (l + r);
        }
        fft.forward(fftScratchRe.data(), fftScratchIm.data());
        unpackPackedStereoSpectrum(fftScratchRe.data(), fftScratchIm.data(),
                                   wolaRe[0].data(), wolaIm[0].data(),
                                   wolaRe[1].data(), wolaIm[1].data());

        // B. Pack Stereo Zero-Phase-Shifted Hann input (circular shift by N/2!) -> 1 complex FFT!
        //    Sample `halfN` lands at index 0 -> exact 0.000-deg center cosine phase in FFT bins!
        for (int i = 0; i < N; ++i)
        {
            const int rIdx = (inPos + i) & (N - 1);
            const int sIdx = (i + halfN) & (N - 1);
            const double w2 = sqrtHann[static_cast<size_t>(i)] * sqrtHann[static_cast<size_t>(i)];
            fftScratchRe[static_cast<size_t>(sIdx)] = inRing[0][static_cast<size_t>(rIdx)] * w2;
            fftScratchIm[static_cast<size_t>(sIdx)] = inRing[1][static_cast<size_t>(rIdx)] * w2;
        }
        fft.forward(fftScratchRe.data(), fftScratchIm.data());
        unpackPackedStereoSpectrum(fftScratchRe.data(), fftScratchIm.data(),
                                   hannRe[0].data(), hannIm[0].data(),
                                   hannRe[1].data(), hannIm[1].data());

        // C. Analyze centered stereo frame (4 hops = 42.7 ms ahead of playback!)
        HarmonicFrame newFrame {};
        newFrame.rawF0   = lastValidF0;
        newFrame.cleanF0 = lastValidF0;
        analyzer.analyzeFrame(monoTimeScratch.data(),
                              hannRe[0].data(), hannIm[0].data(),
                              hannRe[1].data(), hannIm[1].data(),
                              p.minF0Hz, p.maxF0Hz, p.numHarmonics, p.purity01, newFrame);
        if (newFrame.confidence >= 0.20)
            lastValidF0 = newFrame.rawF0;

        // Compute target Low-Cut & Dynamic Crossover for this frame
        const double floorHz = std::clamp(p.lowCutFloorHz, 18.0, 400.0);
        const double c = std::clamp(newFrame.confidence, 0.0, 1.0);
        const double voicedLc = std::clamp(newFrame.rawF0 * std::pow(2.0, p.lowCutOffsetSt / 12.0),
                                           floorHz, 1400.0);
        newFrame.lowCutHz = std::exp((1.0 - c) * std::log(floorHz) + c * std::log(voicedLc));

        const double voicedXov = std::clamp((static_cast<double>(p.numHarmonics) + 0.5) * newFrame.rawF0,
                                            160.0, 15000.0);
        newFrame.xoverHz = voicedXov;

        // Smooth WOLA filter frequencies gently per hop
        smoothLinearLcHz  = std::exp(0.60 * std::log(std::max(smoothLinearLcHz, 15.0))
                                   + 0.40 * std::log(std::max(newFrame.lowCutHz, 15.0)));
        smoothLinearXovHz = std::exp(0.65 * std::log(std::max(smoothLinearXovHz, 140.0))
                                   + 0.35 * std::log(std::max(newFrame.xoverHz, 140.0)));

        // D. Build Zero-Phase (Linear-Phase) Masks for Low-Cut & Dynamic High-Band Crossover
        buildLinearPhaseMasks(p.lowCutSlope, smoothLinearLcHz, smoothLinearXovHz);

        // Precompute exact low-band crossover weight (1 - maskHigh) & low-cut gain at each partial freq
        // so that pureSines + wolaHigh sums to EXACT 0.00 dB flat across the crossover!
        const double binHz = fs / static_cast<double>(N);
        for (int k = 0; k < kTotalSlots; ++k)
        {
            const size_t ku = static_cast<size_t>(k);
            const double bF = std::clamp(newFrame.freq[ku] / binHz, 1.0, static_cast<double>(numBins - 2));
            const int b0 = static_cast<int>(bF);
            const double frac = bF - static_cast<double>(b0);
            const double mLc = maskLc[static_cast<size_t>(b0)] * (1.0 - frac)
                             + maskLc[static_cast<size_t>(b0 + 1)] * frac;
            const double mHi = maskHigh[static_cast<size_t>(b0)] * (1.0 - frac)
                             + maskHigh[static_cast<size_t>(b0 + 1)] * frac;
            // Weight each reconstructed harmonic by mLc (matching post-lowcut level) and (1 - mHi) (matching low-band)
            newFrame.ampL[ku] *= mLc;
            newFrame.ampR[ku] *= mLc;
            newFrame.lowMaskWeight[ku] = 1.0 - mHi;
        }

        // Push frame into lookahead queue
        frameWriteIdx = (frameWriteIdx + 1) & (kQueueSize - 1);
        frameQueue[static_cast<size_t>(frameWriteIdx)] = newFrame;

        // E. Packed Stereo IFFTs (ONLY 2 IFFTs total!) for Dry Low-Cut and High-Band Complement
        applyStereoMaskAndIfft(maskLc.data(), olaDryRing[0].data(), olaDryRing[1].data());

        for (int b = 0; b < numBins; ++b)
            maskHighTotal[static_cast<size_t>(b)] = maskLc[static_cast<size_t>(b)] * maskHigh[static_cast<size_t>(b)];
        applyStereoMaskAndIfft(maskHighTotal.data(), olaHighRing[0].data(), olaHighRing[1].data());

        // F. Non-Causal 5-Frame Lookahead Cleaning & Exact Zero-Lag SMS Hop Dispatch (q[-4] -> q[-3])
        auto getQ = [&](int offsetFromLatest) -> HarmonicFrame&
        {
            return frameQueue[static_cast<size_t>((frameWriteIdx + offsetFromLatest) & (kQueueSize - 1))];
        };

        cleanFramePitchNonCausal(getQ(-4), getQ(-3), getQ(-2));

        const HarmonicFrame& playStart = getQ(-4);
        const HarmonicFrame& playEnd   = getQ(-3);

        smsSynth.beginHop(playStart, playEnd);

        // Publish exact playback-aligned telemetry to GUI
        liveF0       = playStart.cleanF0;
        liveConf     = playStart.confidence;
        liveLowCutHz = playStart.lowCutHz;
        liveXoverHz  = playStart.xoverHz;
        for (int k = 0; k < kMaxHarmonics; ++k)
            liveHarmAmps[static_cast<size_t>(k)] =
                0.5 * (playStart.ampL[static_cast<size_t>(k)] + playStart.ampR[static_cast<size_t>(k)]);
    }

    void unpackPackedStereoSpectrum(const double* zRe, const double* zIm,
                                    double* lRe, double* lIm,
                                    double* rRe, double* rIm) const noexcept
    {
        const int nyq = N / 2;
        lRe[0] = zRe[0]; lIm[0] = 0.0;
        rRe[0] = zIm[0]; rIm[0] = 0.0;
        lRe[nyq] = zRe[nyq]; lIm[nyq] = 0.0;
        rRe[nyq] = zIm[nyq]; rIm[nyq] = 0.0;

        for (int b = 1; b < nyq; ++b)
        {
            const int m = N - b;
            const double zrP = zRe[b], ziP = zIm[b];
            const double zrM = zRe[m], ziM = zIm[m];

            lRe[b] = 0.5 * (zrP + zrM);
            lIm[b] = 0.5 * (ziP - ziM);
            rRe[b] = 0.5 * (ziP + ziM);
            rIm[b] = 0.5 * (zrM - zrP);
        }
    }

    static void cleanFramePitchNonCausal(const HarmonicFrame& prev,
                                         HarmonicFrame& curr,
                                         const HarmonicFrame& next) noexcept
    {
        if (curr.confidence < 0.20)
            return;

        if (prev.confidence > 0.25 && next.confidence > 0.25)
        {
            const double dPrevNext = std::abs(12.0 * std::log2(next.rawF0 / std::max(prev.cleanF0, 20.0)));
            const double dPrevCurr = std::abs(12.0 * std::log2(curr.rawF0 / std::max(prev.cleanF0, 20.0)));
            if (dPrevNext < 1.2 && dPrevCurr > 1.5)
            {
                const double fixedF0 = 0.5 * (prev.cleanF0 + next.rawF0);
                curr.cleanF0 = fixedF0;
                for (int k = 0; k < kTotalSlots; ++k)
                {
                    const size_t ku = static_cast<size_t>(k);
                    curr.freq[ku] = fixedF0 * static_cast<double>(k + 1);
                    curr.ampL[ku] = 0.5 * (prev.ampL[ku] + next.ampL[ku]);
                    curr.ampR[ku] = 0.5 * (prev.ampR[ku] + next.ampR[ku]);
                }
                return;
            }
        }
        curr.cleanF0 = curr.rawF0;

        // Zero-phase 3-frame temporal amplitude smoothing on continuous notes (kills hop-rate AM ripple!)
        if (prev.confidence > 0.25 && next.confidence > 0.25)
        {
            const double dPrev = std::abs(12.0 * std::log2(curr.cleanF0 / std::max(prev.cleanF0, 20.0)));
            const double dNext = std::abs(12.0 * std::log2(next.rawF0   / std::max(curr.cleanF0, 20.0)));
            if (dPrev < 0.75 && dNext < 0.75)
            {
                for (int k = 0; k < kTotalSlots; ++k)
                {
                    const size_t ku = static_cast<size_t>(k);
                    curr.ampL[ku] = 0.22 * prev.ampL[ku] + 0.56 * curr.ampL[ku] + 0.22 * next.ampL[ku];
                    curr.ampR[ku] = 0.22 * prev.ampR[ku] + 0.56 * curr.ampR[ku] + 0.22 * next.ampR[ku];
                }
            }
        }
    }

    void buildLinearPhaseMasks(LowCutSlope slope, double fcLc, double fcXov) noexcept
    {
        const double binHz = fs / static_cast<double>(N);
        const int slopeIdx = static_cast<int>(slope);

        maskLc[0]   = (slopeIdx > 0) ? 0.0 : 1.0;
        maskHigh[0] = 0.0;

        static constexpr int orders[6] = { 2, 4, 6, 8, 12, 16 };
        const bool isBrick = (slope == LowCutSlope::Brickwall);
        const int lcOrder  = (slopeIdx >= 1 && slopeIdx <= 6) ? orders[slopeIdx - 1] : 16;

        for (int b = 1; b < numBins; ++b)
        {
            const double f = static_cast<double>(b) * binHz;

            if (slopeIdx == 0)
            {
                maskLc[static_cast<size_t>(b)] = 1.0;
            }
            else if (isBrick)
            {
                const double trans = std::max(1.5 * binHz, fcLc * 0.06);
                const double u = std::clamp((f - (fcLc - trans)) / (2.0 * trans), 0.0, 1.0);
                maskLc[static_cast<size_t>(b)] = 0.5 - 0.5 * std::cos(3.14159265358979323846 * u);
            }
            else
            {
                const double ratio = fcLc / f;
                const double logR  = std::log10(std::max(ratio, 1e-6));
                const double exp2n = static_cast<double>(2 * lcOrder) * logR;
                const double powTerm = (exp2n > 12.0) ? 1e12 : std::pow(10.0, exp2n);
                maskLc[static_cast<size_t>(b)] = 1.0 / std::sqrt(1.0 + powTerm);
            }

            // Smooth 36 dB/oct linear-phase complementary crossover at (N_harm + 0.5) * f0
            const double rXov = fcXov / f;
            const double logX = std::log10(std::max(rXov, 1e-6));
            const double expX = 12.0 * logX; // 6th-order -> steep enough to isolate H_N & H_{N+1} cleanly
            const double powX = (expX > 12.0) ? 1e12 : std::pow(10.0, expX);
            maskHigh[static_cast<size_t>(b)] = 1.0 / std::sqrt(1.0 + powX);
        }
    }

    void applyStereoMaskAndIfft(const double* realMask,
                                double* olaDestL, double* olaDestR) noexcept
    {
        const int nyq = N / 2;
        fftScratchRe[0] = wolaRe[0][0] * realMask[0];
        fftScratchIm[0] = wolaRe[1][0] * realMask[0];
        fftScratchRe[static_cast<size_t>(nyq)] = wolaRe[0][static_cast<size_t>(nyq)] * realMask[nyq];
        fftScratchIm[static_cast<size_t>(nyq)] = wolaRe[1][static_cast<size_t>(nyq)] * realMask[nyq];

        for (int b = 1; b < nyq; ++b)
        {
            const size_t bu = static_cast<size_t>(b);
            const size_t mu = static_cast<size_t>(N - b);
            const double m  = realMask[b];
            const double lr = wolaRe[0][bu] * m;
            const double li = wolaIm[0][bu] * m;
            const double rr = wolaRe[1][bu] * m;
            const double ri = wolaIm[1][bu] * m;

            fftScratchRe[bu] = lr - ri;
            fftScratchIm[bu] = li + rr;
            fftScratchRe[mu] = lr + ri;
            fftScratchIm[mu] = rr - li;
        }

        fft.inverse(fftScratchRe.data(), fftScratchIm.data());

        for (int i = 0; i < N; ++i)
        {
            const double ws = sqrtHann[static_cast<size_t>(i)] * olaNorm;
            const int dstIdx = (outPos + i) & (N - 1);
            olaDestL[dstIdx] += fftScratchRe[static_cast<size_t>(i)] * ws;
            olaDestR[dstIdx] += fftScratchIm[static_cast<size_t>(i)] * ws;
        }
    }

    double fs = 48000.0;
    int N = 4096;
    int H = 512;
    int numBins = 2049;
    double olaNorm = 0.25;

    FftRadix2 fft;
    HarmonicFrameAnalyzer analyzer;
    PhaseLockedSmsSynth smsSynth;
    MinPhaseLowCut minPhaseLowCut;

    std::vector<double> sqrtHann;
    std::vector<double> inRing[2];
    std::vector<double> rawDelayRing[2];
    std::vector<double> olaDryRing[2];
    std::vector<double> olaHighRing[2];
    std::vector<double> wolaRe[2], wolaIm[2];
    std::vector<double> hannRe[2], hannIm[2];

    std::vector<double> monoTimeScratch;
    std::vector<double> fftScratchRe, fftScratchIm;
    std::vector<double> maskLc, maskHigh, maskHighTotal;

    int inPos = 0;
    int outPos = 0;
    int hopCounter = 0;

    std::array<HarmonicFrame, kQueueSize> frameQueue {};
    int frameWriteIdx = 0;

    double smoothLinearLcHz   = 65.0;
    double smoothLinearXovHz  = 1800.0;
    double smoothMinPhaseLcHz = 65.0;
    double minPhaseCutAlpha   = 0.004;
    double smoothedPlayConf   = 0.0;
    double confSmoothAlpha    = 0.002;

    double lastValidF0  = 180.0;
    double liveF0       = 180.0;
    double liveConf     = 0.0;
    double liveLowCutHz = 65.0;
    double liveXoverHz  = 1800.0;
    std::array<double, kMaxHarmonics> liveHarmAmps {};
};

} // namespace sinefairy

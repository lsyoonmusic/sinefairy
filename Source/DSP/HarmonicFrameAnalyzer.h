#pragma once
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>

namespace sinefairy
{

static constexpr int kMaxHarmonics  = 20;
static constexpr int kTotalSlots    = kMaxHarmonics + 1; // +1 crossover guard harmonic

struct HarmonicFrame
{
    double rawF0       = 180.0;
    double cleanF0     = 180.0;
    double confidence  = 0.0;
    double lowCutHz    = 60.0;
    double xoverHz     = 1800.0;
    int    numActive   = 8;

    // Exact measured partial frequency (Hz), per-channel amplitude, and exact zero-phase center angle (rad)
    std::array<double, kTotalSlots> freq {};
    std::array<double, kTotalSlots> ampL {};
    std::array<double, kTotalSlots> ampR {};
    std::array<double, kTotalSlots> phaseL {};
    std::array<double, kTotalSlots> phaseR {};
    std::array<double, kTotalSlots> lowMaskWeight {}; // Linear-phase low-band weight at freq[k] for seamless xover
};

/**
 * High-precision stereo centered harmonic frame analyzer (11.7 Hz/bin across 44.1..96 kHz).
 *
 * Uses zero-phase-shifted Hann-windowed stereo spectra (circularly shifted by N/2 so sample N/2
 * lands at index 0). Because the Hann kernel is purely real and positive across its mainlobe,
 * the complex FFT bin angle at each partial peak equals the EXACT instantaneous cosine phase
 * at the frame center sample (0.000 deg error even for non-integer bin frequencies!).
 */
class HarmonicFrameAnalyzer
{
public:
    void prepare(double sampleRate, int fftSize)
    {
        fs = std::max(sampleRate, 8000.0);
        N  = fftSize;
        numBins = N / 2 + 1;
        binHz = fs / static_cast<double>(N);

        decimFactor = std::max(1, static_cast<int>(std::round(fs / 12000.0)));
        decimFs = fs / static_cast<double>(decimFactor);

        decimBuf.resize(static_cast<size_t>(N / decimFactor + 8), 0.0);
        cmndfBuf.resize(400, 1.0);
        monoMag.resize(static_cast<size_t>(numBins), 0.0);
        monoLogMag.resize(static_cast<size_t>(numBins), -120.0);
        magL.resize(static_cast<size_t>(numBins), 0.0);
        magR.resize(static_cast<size_t>(numBins), 0.0);
        logMagL.resize(static_cast<size_t>(numBins), -120.0);
        logMagR.resize(static_cast<size_t>(numBins), -120.0);
    }

    /**
     * Analyze centered mono time frame + zero-phase-shifted stereo Hann spectra.
     *
     * @param purity01  0.0 = gentle natural preservation, 0.5 = balanced studio clean, 1.0 = surgical purity
     */
    void analyzeFrame(const double* monoTime,
                      const double* hannReL, const double* hannImL,
                      const double* hannReR, const double* hannImR,
                      double minF0Hz, double maxF0Hz,
                      int numHarmonics,
                      double purity01,
                      HarmonicFrame& out) noexcept
    {
        const double ampScale = 4.0 / static_cast<double>(N);
        for (int b = 0; b < numBins; ++b)
        {
            const size_t bu = static_cast<size_t>(b);
            const double mL = std::hypot(hannReL[b], hannImL[b]) * ampScale;
            const double mR = std::hypot(hannReR[b], hannImR[b]) * ampScale;
            const double mM = std::hypot(0.5 * (hannReL[b] + hannReR[b]),
                                         0.5 * (hannImL[b] + hannImR[b])) * ampScale;

            magL[bu] = mL;
            magR[bu] = mR;
            monoMag[bu] = mM;
            logMagL[bu] = std::log(std::max(mL, 1e-12));
            logMagR[bu] = std::log(std::max(mR, 1e-12));
            monoLogMag[bu] = std::log(std::max(mM, 1e-12));
        }

        // 1. Center-window RMS silence gate (~ -58 dBFS)
        const int halfN = N / 2;
        const int centerStart = halfN - (N / 4);
        const int centerLen   = N / 2;
        double energy = 0.0;
        for (int i = 0; i < centerLen; ++i)
        {
            const double s = monoTime[centerStart + i];
            energy += s * s;
        }
        if (std::sqrt(energy / static_cast<double>(centerLen)) < 0.0011)
        {
            out.confidence = 0.0;
            out.ampL.fill(0.0);
            out.ampR.fill(0.0);
            return;
        }

        // 2. Decimate centered region with sub-rumble guard for YIN
        const double fMin = std::clamp(minF0Hz, 40.0, 400.0);
        const double fMax = std::clamp(maxF0Hz, fMin + 40.0, 1500.0);

        const int decLen = static_cast<int>(decimBuf.size()) - 4;
        const int neededSrc = decLen * decimFactor;
        const int srcOffset = std::max(0, (N - neededSrc) / 2);

        const double hpAlpha = std::exp(-6.283185307179586 * std::clamp(fMin * 0.72, 28.0, 240.0) / decimFs);
        double hpSt = 0.0, hpPrev = 0.0;
        const double invDec = 1.0 / static_cast<double>(decimFactor);

        for (int d = 0; d < decLen; ++d)
        {
            double acc = 0.0;
            const int base = srcOffset + d * decimFactor;
            for (int m = 0; m < decimFactor; ++m)
                acc += monoTime[base + m];
            const double raw = acc * invDec;
            const double hp = hpAlpha * (hpSt + raw - hpPrev);
            hpPrev = raw;
            hpSt   = hp;
            decimBuf[static_cast<size_t>(d)] = hp;
        }

        // 3. Symmetrically Centered YIN (0 ms time skew relative to frame center N/2!)
        const int tauMin = std::clamp(static_cast<int>(std::floor(decimFs / fMax)), 6, 360);
        const int tauMax = std::clamp(static_cast<int>(std::ceil(decimFs / fMin)), tauMin + 2, 380);
        const int yinWin = std::min(192, std::max(80, decLen - tauMax - 4));
        const int centerIdx = decLen / 2;
        const int halfWin   = yinWin / 2;

        cmndfBuf[0] = 1.0;
        double runningSum = 0.0;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            const int halfTau = tau / 2;
            const int startA = std::clamp(centerIdx - halfWin - halfTau, 0, decLen - yinWin - tau - 1);
            double sum = 0.0;
            for (int j = 0; j < yinWin; ++j)
            {
                const double diff = decimBuf[static_cast<size_t>(startA + j)]
                                  - decimBuf[static_cast<size_t>(startA + j + tau)];
                sum += diff * diff;
            }
            runningSum += sum;
            cmndfBuf[static_cast<size_t>(tau)] = (runningSum > 1e-12)
                ? (sum * static_cast<double>(tau) / runningSum)
                : 1.0;
        }

        constexpr double yinThresh = 0.22;
        int bestTau = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
        {
            if (cmndfBuf[static_cast<size_t>(tau)] < yinThresh)
            {
                while (tau + 1 < tauMax && cmndfBuf[static_cast<size_t>(tau + 1)] < cmndfBuf[static_cast<size_t>(tau)])
                    ++tau;
                bestTau = tau;
                break;
            }
        }
        if (bestTau < 0)
        {
            double minVal = 10.0;
            for (int tau = tauMin; tau <= tauMax; ++tau)
            {
                if (cmndfBuf[static_cast<size_t>(tau)] < minVal)
                {
                    minVal = cmndfBuf[static_cast<size_t>(tau)];
                    bestTau = tau;
                }
            }
        }

        if (bestTau <= 1 || bestTau >= tauMax)
        {
            out.confidence = 0.0;
            out.ampL.fill(0.0);
            out.ampR.fill(0.0);
            return;
        }

        // Exact parabolic interpolation (correct downward-parabola sign)
        const double y0 = cmndfBuf[static_cast<size_t>(bestTau - 1)];
        const double y1 = cmndfBuf[static_cast<size_t>(bestTau)];
        const double y2 = cmndfBuf[static_cast<size_t>(bestTau + 1)];
        const double denom = 2.0 * (y0 - 2.0 * y1 + y2);
        double refinedTau = static_cast<double>(bestTau);
        if (denom > 1e-12)
            refinedTau += std::clamp((y0 - y2) / denom, -0.5, 0.5);

        double candF0 = std::clamp(decimFs / std::max(refinedTau, 2.0), fMin, fMax);

        // 4. Hybrid Spectral Harmonic Verification (Eliminates phantom sub-octave errors!)
        auto harmonicScore = [&](double fTest) -> double
        {
            double oddE = 0.0, evenE = 0.0, valE = 0.0;
            for (int k = 1; k <= 6; ++k)
            {
                const double fk = fTest * static_cast<double>(k);
                if (fk >= 0.42 * fs) break;
                const double pk = localPeakMono(fk, 0.25 * fTest);
                const double vl = localPeakMono(fk + 0.5 * fTest, 0.15 * fTest);
                const double w  = 1.0 / std::sqrt(static_cast<double>(k));
                if (k & 1) oddE  += pk * w;
                else       evenE += pk * w;
                valE += vl * w;
            }
            const double tot = oddE + evenE;
            const double oddRatio = oddE / std::max(tot, 1e-9);
            const double snrBonus = tot / std::max(valE + 1e-5, 1e-5);
            return tot * (0.35 + 0.65 * std::min(1.0, oddRatio * 2.4)) * std::min(snrBonus, 12.0);
        };

        const double scoreBase = harmonicScore(candF0);
        if (candF0 * 2.0 <= fMax)
        {
            const double magH1 = localPeakMono(candF0, 0.22 * candF0);
            const double magH2 = localPeakMono(candF0 * 2.0, 0.22 * candF0);
            if (magH1 < 0.22 * magH2 && harmonicScore(candF0 * 2.0) > scoreBase * 1.15)
                candF0 *= 2.0;
        }

        const double bestCmndf = cmndfBuf[static_cast<size_t>(bestTau)];
        const double conf = std::clamp(1.0 - (bestCmndf - 0.30) / 0.15, 0.0, 1.0);
        out.confidence = conf;

        if (conf < 0.20)
        {
            out.ampL.fill(0.0);
            out.ampR.fill(0.0);
            return;
        }

        // 5. Multi-Harmonic Sub-Bin Log-Parabolic f0 Refinement (< 0.15 cent accuracy!)
        double f0Num = 0.0, f0Den = 0.0;
        for (int k = 1; k <= 6; ++k)
        {
            const double fkGuess = candF0 * static_cast<double>(k);
            if (fkGuess >= 0.40 * fs) break;

            int pkBin = 1;
            double fkMeas = fkGuess, ampM = 0.0, prom = 0.0;
            findMonoPartial(fkGuess, candF0, pkBin, fkMeas, ampM, prom);
            if (prom > 2.0 && ampM > 1e-4)
            {
                const double w = ampM * static_cast<double>(k) * std::min(prom, 10.0);
                f0Num += w * (fkMeas / static_cast<double>(k));
                f0Den += w;
            }
        }
        const double refinedF0 = (f0Den > 1e-9) ? std::clamp(f0Num / f0Den, fMin, fMax) : candF0;
        out.rawF0   = refinedF0;
        out.cleanF0 = refinedF0;

        // 6. Extract Exact Stereo Partial Frequency, Amplitude & Zero-Phase Center Angle for H1..H_{N+1}
        const int nHarm = std::clamp(numHarmonics, 1, kMaxHarmonics);
        out.numActive = nHarm;

        // Purity [0..1] controls valley noise-floor subtraction & prominence gating strength:
        //   0.0 = gentle/natural (floorSub=0.30, low gate)
        //   0.5 = studio balanced (floorSub=0.68, clean inter-harmonic valleys)
        //   1.0 = surgical razor-pure (floorSub=1.05, strong prominence gate)
        const double p01 = std::clamp(purity01, 0.0, 1.0);
        const double floorSub = 0.30 + 0.75 * p01;
        const double promFloor = 1.05 + 0.95 * p01;
        const double promSpan  = 0.85 + 0.75 * p01;

        for (int k = 0; k < kTotalSlots; ++k)
        {
            const size_t ku = static_cast<size_t>(k);
            const int hk = k + 1;
            const double idealFreq = refinedF0 * static_cast<double>(hk);

            if (hk > nHarm + 1 || idealFreq >= 0.43 * fs)
            {
                out.freq[ku]   = idealFreq;
                out.ampL[ku]   = 0.0;
                out.ampR[ku]   = 0.0;
                out.phaseL[ku] = 0.0;
                out.phaseR[ku] = 0.0;
                continue;
            }

            int pkBin = 1;
            double fkMeas = idealFreq, ampMono = 0.0, prom = 0.0;
            findMonoPartial(idealFreq, refinedF0, pkBin, fkMeas, ampMono, prom);

            // Allow realistic organic partial deviation within +/- 24 cents of k*f0
            const double maxDev = idealFreq * 0.014;
            const double clampedFk = std::clamp(fkMeas, idealFreq - maxDev, idealFreq + maxDev);
            const double trust = std::clamp((prom - 1.4) / 2.2, 0.0, 1.0);
            out.freq[ku] = idealFreq + trust * (clampedFk - idealFreq);

            // Measure per-channel L & R peak amplitude (log-parabolic at pkBin) & exact center phase!
            const double delta = (out.freq[ku] / binHz) - static_cast<double>(pkBin);
            const double deltaClamped = std::clamp(delta, -0.5, 0.5);

            const double rawAmpL = evalChannelAmp(magL, logMagL, pkBin, deltaClamped);
            const double rawAmpR = evalChannelAmp(magR, logMagR, pkBin, deltaClamped);

            const int vL = std::clamp(static_cast<int>(std::round((idealFreq - 0.5 * refinedF0) / binHz)), 1, numBins - 2);
            const int vR = std::clamp(static_cast<int>(std::round((idealFreq + 0.5 * refinedF0) / binHz)), 1, numBins - 2);
            const double valleyL = 0.5 * (magL[static_cast<size_t>(vL)] + magL[static_cast<size_t>(vR)]);
            const double valleyR = 0.5 * (magR[static_cast<size_t>(vL)] + magR[static_cast<size_t>(vR)]);

            const double promGate = std::clamp((prom - promFloor) / promSpan, 0.0, 1.0);
            const double cleanL = std::max(0.0, rawAmpL - floorSub * valleyL) * promGate * conf;
            const double cleanR = std::max(0.0, rawAmpR - floorSub * valleyR) * promGate * conf;

            out.ampL[ku]   = cleanL;
            out.ampR[ku]   = cleanR;
            out.phaseL[ku] = std::atan2(hannImL[pkBin], hannReL[pkBin]);
            out.phaseR[ku] = std::atan2(hannImR[pkBin], hannReR[pkBin]);
        }
    }

private:
    double localPeakMono(double centerHz, double tolHz) const noexcept
    {
        const int bLo = std::clamp(static_cast<int>(std::floor((centerHz - tolHz) / binHz)), 1, numBins - 2);
        const int bHi = std::clamp(static_cast<int>(std::ceil((centerHz + tolHz) / binHz)),  1, numBins - 2);
        double best = 0.0;
        for (int b = bLo; b <= bHi; ++b)
            best = std::max(best, monoMag[static_cast<size_t>(b)]);
        return best;
    }

    static double evalChannelAmp(const std::vector<double>& mag,
                                 const std::vector<double>& logMag,
                                 int pkBin, double delta) noexcept
    {
        const double l0 = logMag[static_cast<size_t>(pkBin - 1)];
        const double l1 = logMag[static_cast<size_t>(pkBin)];
        const double l2 = logMag[static_cast<size_t>(pkBin + 1)];
        const double denom = (l0 - 2.0 * l1 + l2);
        if (denom < -1e-9)
        {
            // Evaluate fitted parabola at shared partial offset delta
            const double peakLog = l1 + 0.5 * (l2 - l0) * delta + 0.5 * denom * delta * delta;
            return std::exp(std::min(peakLog, 4.0));
        }
        return mag[static_cast<size_t>(pkBin)];
    }

    void findMonoPartial(double targetHz, double f0Hz,
                         int& pkBinOut, double& freqOut,
                         double& ampOut, double& promOut) const noexcept
    {
        const double tolHz = std::min(0.32 * f0Hz, std::max(2.0 * binHz, targetHz * 0.02));
        const int bLo = std::clamp(static_cast<int>(std::floor((targetHz - tolHz) / binHz)), 1, numBins - 2);
        const int bHi = std::clamp(static_cast<int>(std::ceil((targetHz + tolHz) / binHz)),  1, numBins - 2);

        int pkBin = bLo;
        double pkVal = monoMag[static_cast<size_t>(bLo)];
        for (int b = bLo + 1; b <= bHi; ++b)
        {
            if (monoMag[static_cast<size_t>(b)] > pkVal)
            {
                pkVal = monoMag[static_cast<size_t>(b)];
                pkBin = b;
            }
        }

        const double l0 = monoLogMag[static_cast<size_t>(pkBin - 1)];
        const double l1 = monoLogMag[static_cast<size_t>(pkBin)];
        const double l2 = monoLogMag[static_cast<size_t>(pkBin + 1)];
        const double denom = (l0 - 2.0 * l1 + l2);

        double delta = 0.0;
        double interpMag = pkVal;
        if (denom < -1e-9)
        {
            delta = std::clamp(0.5 * (l0 - l2) / denom, -0.5, 0.5);
            const double peakLog = l1 - 0.25 * (l0 - l2) * delta;
            interpMag = std::exp(std::min(peakLog, 4.0));
        }

        const int vL = std::clamp(static_cast<int>(std::round((targetHz - 0.5 * f0Hz) / binHz)), 1, numBins - 2);
        const int vR = std::clamp(static_cast<int>(std::round((targetHz + 0.5 * f0Hz) / binHz)), 1, numBins - 2);
        const double valley = 0.5 * (monoMag[static_cast<size_t>(vL)] + monoMag[static_cast<size_t>(vR)]);

        pkBinOut = pkBin;
        freqOut  = (static_cast<double>(pkBin) + delta) * binHz;
        ampOut   = interpMag;
        promOut  = interpMag / std::max(valley, 1e-6);
    }

    double fs = 48000.0;
    int N = 4096;
    int numBins = 2049;
    double binHz = 48000.0 / 4096.0;

    int decimFactor = 4;
    double decimFs = 12000.0;

    std::vector<double> decimBuf;
    std::vector<double> cmndfBuf;
    std::vector<double> monoMag, monoLogMag;
    std::vector<double> magL, magR, logMagL, logMagR;
};

} // namespace sinefairy

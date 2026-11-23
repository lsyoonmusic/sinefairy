#pragma once
#include "TptSvf.h"
#include <cmath>
#include <algorithm>

namespace sinefairy
{

enum class LowCutSlope : int
{
    Off   = 0,
    dB12  = 1, // 2nd-order Butterworth (1 stage)
    dB24  = 2, // 4th-order Butterworth (2 stages)
    dB36  = 3, // 6th-order Butterworth (3 stages)
    dB48  = 4  // 8th-order Butterworth (4 stages)
};

/**
 * Stereo Pitch-Tracking Dynamic Low-Cut Filter built from cascaded Cytomic TPT SVFs.
 * Moves smoothly below f0 by a user-specified semitone safety offset, resting gracefully
 * at Floor Hz when unvoiced/silent.
 */
class TrackingLowCut
{
public:
    void prepare(double sampleRate)
    {
        fs = std::max(sampleRate, 8000.0);
        cutoffSmoothAlpha = 1.0 - std::exp(-1.0 / (0.008 * fs)); // 8 ms anti-zipper glide
        for (int ch = 0; ch < 2; ++ch)
            for (int st = 0; st < 4; ++st)
                stages[ch][st].prepare(fs);
        reset();
    }

    void reset()
    {
        currentCutoffHz = 65.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int st = 0; st < 4; ++st)
                stages[ch][st].reset();
    }

    inline void processStereoSample(double& left, double& right,
                                    LowCutSlope slope,
                                    double trackedF0, double confidence,
                                    double offsetSemis, double floorHz) noexcept
    {
        // Compute target tracking cutoff:
        //   Voiced:   max(floorHz, f0 * 2^(offsetSemis / 12))
        //   Unvoiced: rests at floorHz
        const double floorClamped = std::clamp(floorHz, 18.0, 400.0);
        const double voicedTarget = std::clamp(trackedF0 * std::pow(2.0, offsetSemis / 12.0),
                                               floorClamped, 1400.0);

        // Log-domain interpolation between floor and voiced target by voicing confidence
        const double c = std::clamp(confidence, 0.0, 1.0);
        const double targetHz = std::exp((1.0 - c) * std::log(floorClamped) + c * std::log(voicedTarget));

        // Smooth cutoff in log-frequency space
        const double logCur = std::log(std::max(currentCutoffHz, 15.0));
        const double logTgt = std::log(std::max(targetHz, 15.0));
        currentCutoffHz = std::exp(logCur + cutoffSmoothAlpha * (logTgt - logCur));

        const int numStages = static_cast<int>(slope);
        if (numStages <= 0)
            return;

        // Exact Butterworth pole Q values for 2nd, 4th, 6th, 8th orders
        static constexpr double qTable[4][4] = {
            { 0.70710678, 0.70710678, 0.70710678, 0.70710678 }, // 12 dB/oct (1 stage)
            { 0.54119610, 1.30656296, 0.70710678, 0.70710678 }, // 24 dB/oct (2 stages)
            { 0.51763809, 0.70710678, 1.93185165, 0.70710678 }, // 36 dB/oct (3 stages)
            { 0.50979558, 0.60134489, 0.89997622, 2.56291545 }  // 48 dB/oct (4 stages)
        };

        const int row = std::clamp(numStages - 1, 0, 3);
        double l = left;
        double r = right;
        for (int st = 0; st < numStages; ++st)
        {
            const double q = qTable[row][st];
            stages[0][st].setParams(currentCutoffHz, q);
            stages[1][st].setParams(currentCutoffHz, q);
            l = stages[0][st].processHighPass(l);
            r = stages[1][st].processHighPass(r);
        }
        left  = l;
        right = r;
    }

    double getCurrentCutoffHz() const noexcept { return currentCutoffHz; }

private:
    double fs = 48000.0;
    double cutoffSmoothAlpha = 0.003;
    double currentCutoffHz = 65.0;
    TptSvf stages[2][4];
};

} // namespace sinefairy

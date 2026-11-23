#pragma once
#include "PitchTrackerYIN.h"
#include "TrackingLowCut.h"
#include "HarmonicResynth.h"
#include <array>
#include <cmath>
#include <algorithm>

namespace sinefairy
{

enum class AuditionMode : int
{
    Normal    = 0,
    SoloSines = 1, // Audition ONLY reconstructed pure harmonic sine waves
    Delta     = 2  // Audition ONLY removed rumble + inter-harmonic noise!
};

struct EngineParams
{
    // Pitch Tracker
    double minF0Hz       = 75.0;
    double maxF0Hz       = 650.0;
    double trackSpeedMs  = 18.0;

    // Pitch-Tracking Low-Cut
    LowCutSlope lowCutSlope = LowCutSlope::dB24;
    double lowCutOffsetSt   = -4.0;  // Semitones below f0 (-12 .. 0 st)
    double lowCutFloorHz    = 65.0;  // Resting cutoff when unvoiced/silent

    // Harmonic Sine Reconstructor
    ResynthMode resynthMode = ResynthMode::CleanReplace;
    int    numHarmonics     = 4;     // 1 .. 6
    double resynthMix       = 0.65;  // 0.0 .. 1.0
    double purityBwHz       = 12.0;  // 5.0 (razor sharp) .. 35.0 (fast/wide)
    std::array<double, HarmonicResynth::kMaxHarmonics> harmGains { 1.0, 1.0, 1.0, 1.0, 1.0, 1.0 };

    // Output / Utility
    AuditionMode audition = AuditionMode::Normal;
    double outputGainDb   = 0.0;
    bool   bypass         = false;
};

class SineFairyEngine
{
public:
    void prepare(double sampleRate)
    {
        fs = std::max(sampleRate, 8000.0);
        tracker.prepare(fs);
        lowCut.prepare(fs);
        resynth.prepare(fs);
        reset();
    }

    void reset()
    {
        tracker.reset();
        lowCut.reset();
        resynth.reset();
    }

    inline void processStereoSample(double& left, double& right, const EngineParams& p) noexcept
    {
        const double dryL = left;
        const double dryR = right;

        // Always keep pitch tracker running so un-bypassing is instant and click-free
        const double mono = 0.5 * (dryL + dryR);
        tracker.processSample(mono, p.minF0Hz, p.maxF0Hz, p.trackSpeedMs);

        if (p.bypass)
            return;

        const double f0   = tracker.getTrackedF0();
        const double conf = tracker.getConfidence();

        // 1. Pitch-Tracking Dynamic Low-Cut (removes sub-fundamental rumble/noise)
        double hpL = dryL;
        double hpR = dryR;
        lowCut.processStereoSample(hpL, hpR, p.lowCutSlope, f0, conf, p.lowCutOffsetSt, p.lowCutFloorHz);

        // Exact sub-fundamental signal stripped by the Low-Cut filter
        const double removedSubL = dryL - hpL;
        const double removedSubR = dryR - hpR;

        // 2. Phase-Locked Low-Harmonic Sinusoidal Resynthesis (cleans inter-harmonic noise!)
        const auto hRes = resynth.processStereoSample(
            hpL, hpR, f0, conf,
            p.numHarmonics, p.resynthMix, p.purityBwHz, p.resynthMode, p.harmGains
        );

        // 3. Audition Routing & Output Gain
        double outL = hRes.outL;
        double outR = hRes.outR;

        if (p.audition == AuditionMode::SoloSines)
        {
            outL = hRes.pureSinesL;
            outR = hRes.pureSinesR;
        }
        else if (p.audition == AuditionMode::Delta)
        {
            // Sum of sub-fundamental rumble removed + inter-harmonic mud removed (zero AP phase leak!)
            outL = removedSubL + hRes.removedLowNoiseL;
            outR = removedSubR + hRes.removedLowNoiseR;
        }

        const double outLin = std::pow(10.0, p.outputGainDb * 0.05);
        left  = outL * outLin;
        right = outR * outLin;
    }

    double getTrackedF0() const noexcept          { return tracker.getTrackedF0(); }
    double getConfidence() const noexcept         { return tracker.getConfidence(); }
    double getLowCutCutoffHz() const noexcept     { return lowCut.getCurrentCutoffHz(); }
    double getXoverHz() const noexcept            { return resynth.getCurrentXoverHz(); }
    double getHarmonicAmp(int k) const noexcept   { return resynth.getHarmonicAmp(k); }

private:
    double fs = 48000.0;
    PitchTrackerYIN tracker;
    TrackingLowCut  lowCut;
    HarmonicResynth resynth;
};

} // namespace sinefairy

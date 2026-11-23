#pragma once
#include <cmath>
#include <algorithm>

namespace sinefairy
{

enum class LowCutSlope : int
{
    Off       = 0,
    dB12      = 1,
    dB24      = 2,
    dB36      = 3,
    dB48      = 4,
    dB72      = 5,
    dB96      = 6,
    Brickwall = 7
};

enum class FilterPhaseMode : int
{
    LinearPhase = 0, // Zero phase distortion via 8x-overlap WOLA
    MinPhase    = 1  // Zero pre-ringing via lookahead-guided Cytomic TPT SVFs
};

class TptSvf
{
public:
    void prepare(double sampleRate)
    {
        fs = std::max(sampleRate, 8000.0);
        invFs = 1.0 / fs;
        reset();
    }

    void reset()
    {
        ic1eq = 0.0;
        ic2eq = 0.0;
    }

    inline void setParams(double cutoffHz, double q) noexcept
    {
        const double fc = std::clamp(cutoffHz, 12.0, 0.44 * fs);
        const double qSafe = std::max(q, 0.25);
        g  = std::tan(3.14159265358979323846 * fc * invFs);
        k  = 1.0 / qSafe;
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    inline double processHighPass(double x) noexcept
    {
        const double v3 = x - ic2eq;
        const double v1 = a1 * ic1eq + a2 * v3;
        const double v2 = ic2eq + a2 * ic1eq + a3 * v3;
        ic1eq = 2.0 * v1 - ic1eq;
        ic2eq = 2.0 * v2 - ic2eq;
        return x - k * v1 - v2;
    }

private:
    double fs = 48000.0;
    double invFs = 1.0 / 48000.0;
    double g = 0.01, k = 1.41421356237, a1 = 1.0, a2 = 0.0, a3 = 0.0;
    double ic1eq = 0.0, ic2eq = 0.0;
};

class MinPhaseLowCut
{
public:
    static constexpr int kMaxStages = 8;

    void prepare(double sampleRate)
    {
        fs = std::max(sampleRate, 8000.0);
        for (int ch = 0; ch < 2; ++ch)
            for (int st = 0; st < kMaxStages; ++st)
                stages[ch][st].prepare(fs);
        reset();
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int st = 0; st < kMaxStages; ++st)
                stages[ch][st].reset();
    }

    inline void processStereoSample(double& left, double& right,
                                    LowCutSlope slope, double cutoffHz) noexcept
    {
        const int idx = static_cast<int>(slope);
        if (idx <= 0)
            return;

        const int row = std::clamp(idx - 1, 0, 5);
        static constexpr int stageCounts[6] = { 1, 2, 3, 4, 6, 8 };
        static constexpr double qTable[6][8] = {
            { 0.70710678, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 },
            { 0.54119610, 1.30656296, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 },
            { 0.51763809, 0.70710678, 1.93185165, 0.0, 0.0, 0.0, 0.0, 0.0 },
            { 0.50979558, 0.60134489, 0.89997622, 2.56291545, 0.0, 0.0, 0.0, 0.0 },
            { 0.50431448, 0.54119610, 0.63023621, 0.82133982, 1.30656296, 3.83064879, 0.0, 0.0 },
            { 0.50241929, 0.52249861, 0.56694403, 0.64682178, 0.78815462, 1.06067769, 1.72244710, 5.10114862 }
        };

        const int numStages = stageCounts[row];
        double l = left;
        double r = right;
        for (int st = 0; st < numStages; ++st)
        {
            const double q = qTable[row][st];
            stages[0][st].setParams(cutoffHz, q);
            stages[1][st].setParams(cutoffHz, q);
            l = stages[0][st].processHighPass(l);
            r = stages[1][st].processHighPass(r);
        }
        left  = l;
        right = r;
    }

private:
    double fs = 48000.0;
    TptSvf stages[2][kMaxStages];
};

} // namespace sinefairy

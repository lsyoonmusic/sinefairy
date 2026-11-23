#pragma once
#include <cmath>
#include <algorithm>

namespace sinefairy
{

/**
 * Zero-delay feedback State Variable Filter (Cytomic TPT SVF).
 * Modulation-safe for real-time pitch-tracking low-cut & dynamic crossovers.
 */
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

    inline double processLowPass(double x) noexcept
    {
        const double v3 = x - ic2eq;
        const double v1 = a1 * ic1eq + a2 * v3;
        const double v2 = ic2eq + a2 * ic1eq + a3 * v3;
        ic1eq = 2.0 * v1 - ic1eq;
        ic2eq = 2.0 * v2 - ic2eq;
        return v2;
    }

private:
    double fs = 48000.0;
    double invFs = 1.0 / 48000.0;
    double g = 0.01, k = 1.41421356237, a1 = 1.0, a2 = 0.0, a3 = 0.0;
    double ic1eq = 0.0, ic2eq = 0.0;
};

} // namespace sinefairy

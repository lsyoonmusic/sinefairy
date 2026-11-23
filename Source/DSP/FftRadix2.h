#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

namespace sinefairy
{

class FftRadix2
{
public:
    void prepare(int fftSize)
    {
        N = fftSize;
        order = 0;
        for (int t = N; t > 1; t >>= 1)
            ++order;

        bitRev.resize(static_cast<size_t>(N));
        for (int i = 0; i < N; ++i)
        {
            int x = i, rev = 0;
            for (int b = 0; b < order; ++b)
            {
                rev = (rev << 1) | (x & 1);
                x >>= 1;
            }
            bitRev[static_cast<size_t>(i)] = rev;
        }

        const int half = N / 2;
        twRe.resize(static_cast<size_t>(half));
        twIm.resize(static_cast<size_t>(half));
        constexpr double twoPi = 6.2831853071795864769;
        for (int i = 0; i < half; ++i)
        {
            const double ang = -twoPi * static_cast<double>(i) / static_cast<double>(N);
            twRe[static_cast<size_t>(i)] = std::cos(ang);
            twIm[static_cast<size_t>(i)] = std::sin(ang);
        }
    }

    int getSize() const noexcept { return N; }

    void forward(double* re, double* im) const noexcept
    {
        transform(re, im, false);
    }

    void inverse(double* re, double* im) const noexcept
    {
        transform(re, im, true);
        const double invN = 1.0 / static_cast<double>(N);
        for (int i = 0; i < N; ++i)
        {
            re[i] *= invN;
            im[i] *= invN;
        }
    }

private:
    void transform(double* re, double* im, bool invert) const noexcept
    {
        for (int i = 0; i < N; ++i)
        {
            const int j = bitRev[static_cast<size_t>(i)];
            if (i < j)
            {
                std::swap(re[i], re[j]);
                std::swap(im[i], im[j]);
            }
        }

        const double sign = invert ? -1.0 : 1.0;

        for (int len = 2; len <= N; len <<= 1)
        {
            const int half = len >> 1;
            const int step = N / len;
            for (int i = 0; i < N; i += len)
            {
                for (int j = 0; j < half; ++j)
                {
                    const int twIdx = j * step;
                    const double wr = twRe[static_cast<size_t>(twIdx)];
                    const double wi = sign * twIm[static_cast<size_t>(twIdx)];

                    const int u = i + j;
                    const int v = u + half;

                    const double vr = re[v];
                    const double vi = im[v];
                    const double tr = wr * vr - wi * vi;
                    const double ti = wr * vi + wi * vr;

                    const double ur = re[u];
                    const double ui = im[u];
                    re[u] = ur + tr;
                    im[u] = ui + ti;
                    re[v] = ur - tr;
                    im[v] = ui - ti;
                }
            }
        }
    }

    int N = 4096;
    int order = 12;
    std::vector<int> bitRev;
    std::vector<double> twRe, twIm;
};

} // namespace sinefairy

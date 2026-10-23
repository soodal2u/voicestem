// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 soodal - VoiceStem (https://github.com/soodal2u/voicestem)

#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// Push/pull windowed-sinc resampler for a fixed rate pair.
// Output sample j is the band-limited input evaluated at time j * inRate / outRate,
// so the conversion adds no group delay; callers only wait for kernel look-ahead.
class StreamResampler
{
public:
    void prepare (double inRate, double outRate, int maxPush)
    {
        ratio = inRate / outRate;
        const double cutoff = std::min (1.0, 1.0 / ratio) * 0.95;
        halfWidth = zeroCrossings / cutoff;

        const int size = (int) std::ceil (halfWidth * tableRes) + 2;
        table.assign ((size_t) size, 0.0f);
        const double beta = 9.0;
        const double i0b = besselI0 (beta);
        for (int i = 0; i < size; ++i)
        {
            const double t = (double) i / tableRes;
            if (t > halfWidth)
                break;
            const double x = cutoff * t;
            const double sinc = x == 0.0 ? 1.0 : std::sin (pi * x) / (pi * x);
            const double r = t / halfWidth;
            const double win = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0b;
            table[(size_t) i] = (float) (cutoff * sinc * win);
        }

        data.clear();
        data.reserve ((size_t) (maxPush + 4 * (int) halfWidth + compactThreshold + 64));
        dataStart = 0;
        totalIn = 0;
        pos = 0.0;
    }

    // Input samples the resampler must hold back before it can emit output.
    int lookahead() const { return (int) std::ceil (halfWidth) + 1; }

    void push (const float* in, int n)
    {
        data.insert (data.end(), in, in + n);
        totalIn += n;
    }

    // Emits as many output samples as the pushed input allows, up to maxOut.
    int pull (float* out, int maxOut)
    {
        int produced = 0;
        while (produced < maxOut && pos + halfWidth + 1.0 < (double) totalIn)
        {
            const long long first = (long long) std::ceil (pos - halfWidth);
            const long long last = (long long) std::floor (pos + halfWidth);
            double acc = 0.0;
            for (long long k = std::max (first, 0LL); k <= last; ++k)
                acc += data[(size_t) (k - dataStart)] * kernel (pos - (double) k);
            out[produced++] = (float) acc;
            pos += ratio;
        }

        const long long keepFrom = (long long) std::floor (pos - halfWidth) - 1;
        if (keepFrom - dataStart > compactThreshold)
        {
            data.erase (data.begin(), data.begin() + (keepFrom - dataStart));
            dataStart = keepFrom;
        }
        return produced;
    }

private:
    static constexpr double pi = 3.14159265358979323846;
    static constexpr double zeroCrossings = 16.0;
    static constexpr int tableRes = 512;
    static constexpr long long compactThreshold = 4096;

    float kernel (double t) const
    {
        const double a = std::abs (t) * tableRes;
        const auto i = (size_t) a;
        if (i + 1 >= table.size())
            return 0.0f;
        const float f = (float) (a - (double) i);
        return table[i] + f * (table[i + 1] - table[i]);
    }

    static double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 50; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
            if (term < 1e-12 * sum)
                break;
        }
        return sum;
    }

    double ratio = 1.0, halfWidth = zeroCrossings;
    std::vector<float> table, data;
    long long dataStart = 0, totalIn = 0;
    double pos = 0.0;
};

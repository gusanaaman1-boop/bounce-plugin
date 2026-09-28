#include "Resampler.h"

#include <algorithm>
#include <cmath>

namespace bounce
{
namespace
{
double besselI0 (double x) noexcept
{
    double sum = 1.0, term = 1.0;
    const auto half = x * 0.5;

    for (int k = 1; k < 50; ++k)
    {
        term *= (half / k) * (half / k);
        sum += term;
        if (term < sum * 1.0e-12)
            break;
    }

    return sum;
}

struct Kernel
{
    std::array<float, Resampler::tableSize> values {};

    Kernel() noexcept
    {
        constexpr double beta = 8.0;
        const auto norm = besselI0 (beta);
        const double pi = 3.14159265358979323846;

        for (int i = 0; i < Resampler::tableSize; ++i)
        {
            const auto x = (double) i / Resampler::tableResolution;        // in zero crossings
            if (x >= Resampler::zeroCrossings)
            {
                values[(size_t) i] = 0.0f;
                continue;
            }

            const auto sinc = x == 0.0 ? 1.0 : std::sin (pi * x) / (pi * x);
            const auto w = x / Resampler::zeroCrossings;
            const auto window = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - w * w))) / norm;
            values[(size_t) i] = (float) (sinc * window);
        }
    }
};

// Kernel value at |distance| d zero crossings, linearly interpolated from the table.
inline float kernelAt (const float* t, double d) noexcept
{
    const auto x = d * Resampler::tableResolution;
    const auto i = (int) x;
    if (i >= Resampler::tableSize - 1)
        return 0.0f;
    const auto f = (float) (x - i);
    return t[i] + f * (t[i + 1] - t[i]);
}
}

const std::array<float, Resampler::tableSize>& Resampler::table() noexcept
{
    static const Kernel kernel;
    return kernel.values;
}

void Resampler::initialise() noexcept
{
    (void) table();
}

void Resampler::read2 (const float* left, const float* right, int64_t length,
                       double pos, double rate, float& outL, float& outR) noexcept
{
    outL = outR = 0.0f;

    if (! std::isfinite (pos) || length <= 0)
        return;

    const auto base = (int64_t) std::floor (pos);
    const auto frac = pos - (double) base;

    // Unpitched fast path: an exact copy.
    if (rate == 1.0 && frac == 0.0)
    {
        if (base >= 0 && base < length)
        {
            outL = left[base];
            outR = right != nullptr ? right[base] : 0.0f;
        }
        return;
    }

    const auto cutoff = rate > 1.0 ? 1.0 / std::min (rate, 2.0) : 1.0;
    const auto reach = (int) std::ceil (zeroCrossings / cutoff);
    const auto* t = table().data();

    const auto first = std::max<int64_t> (base - reach + 1, 0);
    const auto last  = std::min<int64_t> (base + reach, length - 1);

    double accL = 0.0, accR = 0.0;

    for (auto k = first; k <= last; ++k)
    {
        const auto d = std::abs ((double) k - pos) * cutoff;
        const auto h = kernelAt (t, d);
        accL += (double) h * left[k];
        if (right != nullptr)
            accR += (double) h * right[k];
    }

    // Scale by the cutoff: a stretched kernel sums to 1/c, so this keeps unity DC gain. Near
    // the excerpt edges the missing taps are simply zeros - the excerpt is faded there.
    outL = (float) (accL * cutoff);
    outR = (float) (accR * cutoff);
}

float Resampler::read1 (const float* data, int64_t length, double pos, double rate) noexcept
{
    float l = 0.0f, r = 0.0f;
    read2 (data, nullptr, length, pos, rate, l, r);
    return l;
}
}

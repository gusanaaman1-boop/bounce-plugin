#include "OnsetDetector.h"

#include <algorithm>
#include <cmath>

namespace bounce
{
namespace
{
float onePole (double ms, double sampleRate) noexcept
{
    return (float) (1.0 - std::exp (-1.0 / (ms * 0.001 * sampleRate)));
}
}

void OnsetDetector::prepare (double sampleRate) noexcept
{
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    fastAttack  = 1.0f;                 // instant: an onset is found on its own sample at any rate
    fastRelease = onePole (1.0, sr);
    slowCoeff   = onePole (30.0, sr);
    reset();
}

void OnsetDetector::reset() noexcept
{
    fast = slow = 0.0f;
    armed = true;
    lastTrigger = INT64_MIN / 2;
}

void OnsetDetector::setParameters (float thresholdDb, float retriggerMs) noexcept
{
    const auto db = std::isfinite (thresholdDb) ? std::clamp (thresholdDb, -48.0f, -6.0f) : -24.0f;
    threshold = std::pow (10.0f, db / 20.0f);
    rearmLevel = threshold * 0.5f;
    const auto ms = std::isfinite (retriggerMs) ? std::clamp (retriggerMs, 20.0f, 500.0f) : 90.0f;
    retriggerSamples = (int64_t) (ms * 0.001 * sr + 0.5);
}

bool OnsetDetector::process (float peak, int64_t clock) noexcept
{
    const auto x = std::isfinite (peak) ? std::min (std::abs (peak), 64.0f) : 0.0f;
    const auto previous = fast;

    fast += (x > fast ? fastAttack : fastRelease) * (x - fast);
    // The slow envelope follows the fast one, not the raw signal: stationary noise then has a
    // steady fast envelope that the slow one matches, so it cannot keep re-triggering.
    slow += slowCoeff * (fast - slow);

    // Flush denormals from a long silence.
    if (fast < 1.0e-12f) fast = 0.0f;
    if (slow < 1.0e-12f) slow = 0.0f;

    if (! armed && (fast < rearmLevel || fast < slow))
        armed = true;

    if (armed && fast > previous && fast >= threshold && fast >= 2.0f * slow
        && clock - lastTrigger >= retriggerSamples)
    {
        armed = false;
        lastTrigger = clock;
        return true;
    }

    return false;
}
}

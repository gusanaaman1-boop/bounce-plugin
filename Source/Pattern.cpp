#include "Pattern.h"
#include "Parameters.h"

#include <algorithm>
#include <cmath>

namespace bounce
{
namespace
{
template <typename T>
T finiteOr (T v, T fallback) noexcept  { return std::isfinite (v) ? v : fallback; }
}

bool repairTapTimes (std::array<float, 8>& times) noexcept
{
    const auto original = times;

    for (int i = 0; i < 8; ++i)
        if (! std::isfinite (times[(size_t) i]))
            times[(size_t) i] = defaultTapTime (i);

    // Forward: each slot at least one margin above its predecessor, and leaving room for the
    // slots after it below the maximum.
    for (int i = 0; i < 8; ++i)
    {
        const auto lo = i == 0 ? tapTimeMin : times[(size_t) i - 1] + minTimeSeparation;
        const auto hi = tapTimeMax - (float) (7 - i) * minTimeSeparation;
        times[(size_t) i] = std::clamp (times[(size_t) i], lo, std::max (lo, hi));
    }

    return times != original;
}

double masterTimeForNominal (double t, double phrase, double p, int repeats) noexcept
{
    if (! (phrase > 0.0) || ! (p > 0.0) || repeats <= 0 || ! std::isfinite (t))
        return 0.0;

    const auto u = std::pow (std::max (0.0, t) / phrase, 1.0 / p);
    return (double) repeats / 8.0 * u;
}

Schedule computeSchedule (const PatternSettings& s, double sampleRate, double bpm) noexcept
{
    Schedule out;
    const auto sr = finiteOr (sampleRate, 48000.0) > 1000.0 ? sampleRate : 48000.0;
    const auto n = std::clamp (s.repeats, 2, numSlots);

    out.sampleRate = sr;
    out.repeats = n;
    out.bpm = (std::isfinite (bpm) && bpm > 0.0) ? std::clamp (bpm, 10.0, 999.0) : fallbackBpm;

    const auto division = std::clamp (s.division, 0, numDivisions - 1);
    const auto freeMs = std::clamp (finiteOr (s.freeMs, 125.0f), 30.0f, 1000.0f);

    out.intervalSamples = s.sync ? sr * 60.0 / out.bpm * divisionBeats[(size_t) division]
                                 : sr * (double) freeMs * 0.001;
    out.phraseSamples = (double) n * out.intervalSamples;

    const auto m = std::clamp ((double) finiteOr (s.motion, 0.0f), -100.0, 100.0) / 100.0;
    out.warpPower = std::pow (2.0, -m);

    out.sourceSamples = std::max<int64_t> (1, samplesFromMs (std::clamp ((double) finiteOr (s.sourceMs, 100.0f), 20.0, 1000.0), sr));
    // TIGHT keeps only the hit itself: the capture ends tightMs after the onset, so the excerpt
    // is short (a dry hit, not an echo) and source safety lets the first repeat come earlier.
    const auto tight = std::clamp ((double) finiteOr (s.tightMs, 0.0f), 0.0, 200.0);
    if (tight >= 0.5)
    {
        out.tightSamples = samplesFromMs (tight, sr);
        out.sourceSamples = std::min (out.sourceSamples, samplesFromMs (preRollMs, sr) + out.tightSamples);
    }
    out.choke = s.choke;

    out.preRollSamples = std::min<int64_t> (samplesFromMs (preRollMs, sr), out.sourceSamples - 1);

    auto times = s.time;
    repairTapTimes (times);

    // Nominal warped positions on the eight-interval master grid.
    std::array<double, 8> t {};
    double latest = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const auto u = 8.0 * (double) times[(size_t) i] / (double) n;
        t[(size_t) i] = out.phraseSamples * std::pow (u, out.warpPower);
        latest = std::max (latest, t[(size_t) i]);
    }

    // 16 s cap: scale the whole set proportionally.
    const auto cap = maxPhraseSeconds * sr;
    if (latest > cap)
    {
        out.capScale = cap / latest;
        for (int i = 0; i < n; ++i)
            t[(size_t) i] *= out.capScale;
    }

    std::array<int64_t, 8> d {};
    for (int i = 0; i < n; ++i)
    {
        out.taps[(size_t) i].nominalSamples = t[(size_t) i];
        d[(size_t) i] = (int64_t) std::llround (t[(size_t) i]);
    }

    // Source safety: the whole capture must exist before the first repeat starts reading.
    const auto earliestFirst = out.sourceSamples + samplesFromMs (safetyMarginMs, sr);
    if (d[0] < earliestFirst)
    {
        out.safetyShiftSamples = earliestFirst - d[0];
        for (int i = 0; i < n; ++i)
            d[(size_t) i] += out.safetyShiftSamples;
    }

    // Minimum 12 ms between adjacent repeats, pushing later taps only as needed.
    const auto minGap = samplesFromMs (minimumGapMs, sr);
    for (int i = 1; i < n; ++i)
    {
        if (d[(size_t) i] < d[(size_t) i - 1] + minGap)
        {
            d[(size_t) i] = d[(size_t) i - 1] + minGap;
            out.gapPushed = true;
        }
    }

    const auto decay = std::clamp ((double) finiteOr (s.decayDb, -18.0f), -36.0, 0.0);
    const auto pitchPath = std::clamp ((double) finiteOr (s.pitchPathSt, 0.0f), -12.0, 12.0);

    for (int i = 0; i < n; ++i)
    {
        auto& tap = out.taps[(size_t) i];
        const auto q = (double) i / (double) (n - 1);
        const auto level = std::clamp ((double) finiteOr (s.levelDb[(size_t) i], 0.0f), -48.0, 6.0) + decay * q;
        const auto pitch = std::clamp (std::clamp ((double) finiteOr (s.pitchSt[(size_t) i], 0.0f), -12.0, 12.0)
                                       + pitchPath * q, -12.0, 12.0);

        tap.active = true;
        tap.on = s.on[(size_t) i];
        tap.delaySamples = d[(size_t) i];
        tap.levelDb = (float) level;
        tap.gain = tap.on ? (float) std::pow (10.0, level / 20.0) : 0.0f;
        tap.pitchSt = (float) pitch;
        tap.rate = pitch == 0.0 ? 1.0 : std::pow (2.0, pitch / 12.0);
        tap.lengthSamples = (int64_t) std::ceil ((double) out.sourceSamples / tap.rate);

        out.endSamples = std::max (out.endSamples, tap.delaySamples - out.preRollSamples + tap.lengthSamples);
    }

    out.firstDelaySamples = d[0];
    out.lastDelaySamples = d[(size_t) n - 1];
    return out;
}
}

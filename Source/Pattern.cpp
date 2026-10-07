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

namespace
{
// How much later the whole pattern must move so no tap reads audio before it is captured.
int64_t safetyDeficit (const Schedule& s) noexcept
{
    const auto margin = samplesFromMs (safetyMarginMs, s.sampleRate);
    int64_t deficit = 0;

    for (int i = 0; i < s.repeats; ++i)
    {
        const auto& tap = s.taps[(size_t) i];
        int64_t earliest = 0;
        if (i == 0)
            earliest = s.sourceSamples + margin;
        if (tap.reverse)
            earliest = std::max (earliest, s.sourceSamples - s.preRollSamples + margin + tap.swellSamples);
        deficit = std::max (deficit, earliest - tap.delaySamples);
    }

    return deficit;
}

void finishTimes (Schedule& s) noexcept
{
    s.endSamples = 0;
    for (int i = 0; i < s.repeats; ++i)
    {
        const auto& tap = s.taps[(size_t) i];
        const auto end = tap.reverse ? tap.delaySamples + (int64_t) std::ceil ((double) (s.preRollSamples + 1) / tap.rate)
                                     : tap.delaySamples - s.preRollSamples + tap.lengthSamples;
        s.endSamples = std::max (s.endSamples, end);
    }

    s.firstDelaySamples = s.taps[0].delaySamples;
    s.lastDelaySamples = s.taps[(size_t) s.repeats - 1].delaySamples;
}
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

    const auto decay = std::clamp ((double) finiteOr (s.decayDb, -18.0f), -36.0, 0.0);
    const auto pitchPath = std::clamp ((double) finiteOr (s.pitchPathSt, 0.0f), -12.0, 12.0);
    out.quantize = s.quantize && s.sync;   // a free-running interval has no grid to lock to

    for (int i = 0; i < n; ++i)
    {
        auto& tap = out.taps[(size_t) i];
        const auto q = (double) i / (double) (n - 1);
        const auto level = std::clamp ((double) finiteOr (s.levelDb[(size_t) i], 0.0f), -48.0, 6.0) + decay * q;
        const auto pitch = std::clamp (std::clamp ((double) finiteOr (s.pitchSt[(size_t) i], 0.0f), -12.0, 12.0)
                                       + pitchPath * q, -12.0, 12.0);

        tap.active = true;
        tap.on = s.on[(size_t) i];
        tap.nominalSamples = t[(size_t) i];
        tap.delaySamples = (int64_t) std::llround (t[(size_t) i]);
        tap.levelDb = (float) level;
        tap.gain = tap.on ? (float) std::pow (10.0, level / 20.0) : 0.0f;
        tap.pitchSt = (float) pitch;
        tap.rate = pitch == 0.0 ? 1.0 : std::pow (2.0, pitch / 12.0);
        tap.lengthSamples = (int64_t) std::ceil ((double) out.sourceSamples / tap.rate);
        tap.reverse = s.reverse[(size_t) i];
        // Backwards, the transient (excerpt index preRoll) lands on delaySamples and everything
        // after it in the capture - the tail - is heard before it, as a swell.
        tap.swellSamples = tap.reverse ? (int64_t) std::llround ((double) (out.sourceSamples - 1 - out.preRollSamples) / tap.rate) : 0;
    }

    // Source safety: the whole capture must exist before a repeat starts reading it. The first
    // tap always waits for the full capture plus 5 ms (spec 4); a reversed tap starts reading
    // swellSamples before its time, from the END of the capture, so it needs that much more.
    // Every tap moves by the same amount, keeping the pattern's shape.
    out.safetyShiftSamples = safetyDeficit (out);
    for (int i = 0; i < n; ++i)
        out.taps[(size_t) i].delaySamples += out.safetyShiftSamples;

    // Minimum 12 ms between adjacent repeats, pushing later taps only as needed.
    const auto minGap = samplesFromMs (minimumGapMs, sr);
    for (int i = 1; i < n; ++i)
    {
        auto& d = out.taps[(size_t) i].delaySamples;
        if (d < out.taps[(size_t) i - 1].delaySamples + minGap)
        {
            d = out.taps[(size_t) i - 1].delaySamples + minGap;
            out.gapPushed = true;
        }
    }

    finishTimes (out);
    return out;
}

void applyGridOffset (Schedule& s, double offsetSamples) noexcept
{
    if (! std::isfinite (offsetSamples))
        return;

    const auto offset = (int64_t) std::llround (offsetSamples);
    for (int i = 0; i < s.repeats; ++i)
        s.taps[(size_t) i].delaySamples += offset;

    // Too early to have the audio yet: later by whole grid intervals, never off the grid.
    const auto step = std::max<int64_t> (1, (int64_t) std::llround (s.intervalSamples));
    int64_t extra = 0;
    if (const auto deficit = safetyDeficit (s); deficit > 0)
        extra = (deficit + step - 1) / step * step;

    for (int i = 0; i < s.repeats; ++i)
        s.taps[(size_t) i].delaySamples += extra;

    s.quantizeOffsetSamples = offset + extra;
    finishTimes (s);
}
}

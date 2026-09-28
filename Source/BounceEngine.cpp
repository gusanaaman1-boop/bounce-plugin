#include "BounceEngine.h"
#include "Resampler.h"

#include <algorithm>
#include <cmath>

namespace bounce
{
void BounceEngine::prepare (double sampleRate, int numChannels)
{
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    numCh = std::clamp (numChannels, 1, 2);
    preRollMax = samplesFromMs (preRollMs, sr);
    capacity = (int64_t) std::ceil (sr) + preRollMax + 16;
    cancelFadeSamples = std::max<int64_t> (1, samplesFromMs (cancelFadeMs, sr));

    pool.assign ((size_t) (capacity * 2 * maxEvents), 0.0f);
    for (int e = 0; e < maxEvents; ++e)
    {
        events[(size_t) e].data[0] = pool.data() + (size_t) (capacity * 2 * e);
        events[(size_t) e].data[1] = events[(size_t) e].data[0] + capacity;
    }

    history.assign ((size_t) historySize * 2, 0.0f);
    detector.prepare (sr);
    Resampler::initialise();
    scheduleDirty = true;
    reset();
}

void BounceEngine::reset() noexcept
{
    for (auto& e : events)
    {
        e.inUse = e.capturing = false;
        e.fadeEnd = -1;
    }

    std::fill (history.begin(), history.end(), 0.0f);
    detector.reset();
    active.store (0, std::memory_order_relaxed);
}

void BounceEngine::setPattern (const PatternSettings& s, double bpm) noexcept
{
    pendingSettings = s;
    pendingBpm = bpm;
    scheduleDirty = true;
}

void BounceEngine::setDetecting (bool shouldDetect) noexcept
{
    if (detecting && ! shouldDetect)
        cancelAll();

    if (! detecting && shouldDetect)
        detector.reset();

    detecting = shouldDetect;
}

void BounceEngine::cancelFrom (int64_t clock) noexcept
{
    for (auto& e : events)
    {
        if (! e.inUse)
            continue;

        if (e.fadeEnd < 0)
            cancelled.fetch_add (1, std::memory_order_relaxed);

        if (e.capturing)
        {
            // Nothing of it has sounded yet.
            e.inUse = e.capturing = false;
            continue;
        }

        if (e.fadeEnd < 0)
            e.fadeEnd = clock + cancelFadeSamples;
    }
}

void BounceEngine::startEvent (int64_t onsetClock) noexcept
{
    triggers.fetch_add (1, std::memory_order_relaxed);

    if (scheduleDirty)
    {
        cachedSchedule = computeSchedule (pendingSettings, sr, pendingBpm);
        scheduleDirty = false;
    }

    const auto& s = cachedSchedule;
    bool anyTap = false;
    for (int i = 0; i < s.repeats; ++i)
        anyTap = anyTap || (s.taps[(size_t) i].on && s.taps[(size_t) i].gain > 0.0f);

    lastTriggerSchedule = s;
    lastOnset = onsetClock;

    // CHOKE: earlier events stop where this hit's first repeat begins (a 5 ms fade ending
    // there), so one roll hands over to the next instead of piling up - and a hit that comes
    // before the previous roll has finished never silences it early.
    if (s.choke)
        cancelFrom (onsetClock + s.firstDelaySamples - s.preRollSamples - cancelFadeSamples);

    if (! anyTap)
        return;

    Event* slot = nullptr;
    for (auto& e : events)
        if (! e.inUse) { slot = &e; break; }

    // Full pool: drop the NEW event. Stealing a sounding one would click.
    if (slot == nullptr)
    {
        dropped.fetch_add (1, std::memory_order_relaxed);
        return;
    }

    auto& e = *slot;
    e.inUse = true;
    e.capturing = true;
    e.fadeEnd = -1;
    e.onset = onsetClock;
    e.length = std::clamp<int64_t> (s.sourceSamples, 2, capacity);
    e.preRoll = std::clamp<int64_t> (s.preRollSamples, 0, std::min (preRollMax, e.length - 1));
    e.end = onsetClock;
    e.tight = s.tightSamples > 0;

    for (int i = 0; i < 8; ++i)
    {
        auto& tap = e.taps[(size_t) i];
        const auto& plan = s.taps[(size_t) i];
        tap.play = i < s.repeats && plan.on && plan.gain > 0.0f && std::isfinite (plan.gain);
        tap.rate = std::isfinite (plan.rate) ? std::clamp (plan.rate, 0.5, 2.0) : 1.0;
        tap.gain = tap.play ? plan.gain : 0.0f;
        // The excerpt's onset (index preRoll) lands exactly on onset + delay.
        tap.start = onsetClock + plan.delaySamples - e.preRoll;
        tap.length = (int64_t) std::ceil ((double) e.length / tap.rate);

        if (tap.play)
            e.end = std::max (e.end, tap.start + tap.length);
    }

    // The 8 ms before the onset, from the history ring.
    const auto mask = (int64_t) historySize - 1;
    for (int ch = 0; ch < numCh; ++ch)
    {
        const auto* h = history.data() + (size_t) ch * historySize;
        for (int64_t k = 0; k < e.preRoll; ++k)
            e.data[ch][k] = h[(onsetClock - e.preRoll + k) & mask];
    }

    e.written = e.preRoll;
}

void BounceEngine::finishCapture (Event& e) noexcept
{
    e.capturing = false;

    // 0.5 ms entrance fade on the pre-onset edge only, 5 ms exit fade at the end. Both scale
    // down for short windows so they never reach the transient at index preRoll.
    const auto fadeIn  = std::min<int64_t> (samplesFromMs (0.5, sr), e.preRoll / 2);
    // With TIGHT the hit rings out over the last 60 % of its short window instead of 5 ms.
    const auto fadeOut = e.tight ? (e.length - e.preRoll) * 6 / 10
                                 : std::min<int64_t> (samplesFromMs (5.0, sr), (e.length - e.preRoll) / 2);

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* d = e.data[ch];

        for (int64_t k = 0; k < fadeIn; ++k)
            d[k] *= (float) (0.5 - 0.5 * std::cos (3.14159265358979 * (double) k / (double) fadeIn));

        for (int64_t k = 0; k < fadeOut; ++k)
            d[e.length - 1 - k] *= (float) (0.5 - 0.5 * std::cos (3.14159265358979 * (double) k / (double) fadeOut));
    }
}

void BounceEngine::renderEvent (Event& e, float* const* wet, int numSamples) noexcept
{
    const auto blockStart = now;
    const auto blockEnd = now + numSamples;
    const float* left = e.data[0];
    const float* right = numCh > 1 ? e.data[1] : nullptr;

    for (auto& tap : e.taps)
    {
        if (! tap.play)
            continue;

        const auto from = std::max (blockStart, tap.start);
        const auto to = std::min (blockEnd, tap.start + tap.length);

        if (from >= to)
            continue;

        for (auto c = from; c < to; ++c)
        {
            const auto pos = (double) (c - tap.start) * tap.rate;

            if (pos >= (double) e.length)
                break;

            auto g = tap.gain;
            if (e.fadeEnd >= 0)
                g *= std::clamp ((float) (e.fadeEnd - c) / (float) cancelFadeSamples, 0.0f, 1.0f);

            float l = 0.0f, r = 0.0f;
            Resampler::read2 (left, right, e.length, pos, tap.rate, l, r);

            const auto i = (size_t) (c - blockStart);
            wet[0][i] += g * l;
            if (numCh > 1)
                wet[1][i] += g * r;
        }
    }
}

void BounceEngine::process (const float* const* input, float* const* wet, int numSamples) noexcept
{
    if (numSamples <= 0 || pool.empty())
        return;

    for (int ch = 0; ch < numCh; ++ch)
        std::fill (wet[ch], wet[ch] + numSamples, 0.0f);

    const auto mask = (int64_t) historySize - 1;
    const float* inL = input[0];
    const float* inR = numCh > 1 ? input[1] : nullptr;
    auto* histL = history.data();
    auto* histR = history.data() + historySize;

    // Pass 1 - the original input: detector, history and live captures. The input is read in
    // full before any output is written, so in-place host buffers are safe.
    for (int i = 0; i < numSamples; ++i)
    {
        const auto clockNow = now + i;
        const auto xl = std::isfinite (inL[i]) ? inL[i] : 0.0f;
        const auto xr = inR != nullptr ? (std::isfinite (inR[i]) ? inR[i] : 0.0f) : xl;

        if (detecting && detector.process (std::max (std::abs (xl), std::abs (xr)), clockNow))
            startEvent (clockNow);

        histL[clockNow & mask] = xl;
        histR[clockNow & mask] = xr;

        for (auto& e : events)
        {
            if (! e.capturing)
                continue;

            e.data[0][e.written] = xl;
            if (numCh > 1)
                e.data[1][e.written] = xr;

            if (++e.written >= e.length)
                finishCapture (e);
        }
    }

    // Pass 2 - scheduled taps from finished captures. Source safety guarantees no tap starts
    // before its capture is complete; a still-capturing event has nothing to play yet.
    int count = 0;
    for (auto& e : events)
    {
        if (! e.inUse)
            continue;

        if (! e.capturing)
            renderEvent (e, wet, numSamples);

        const auto blockEnd = now + numSamples;
        const auto finished = ! e.capturing && blockEnd >= e.end;
        const auto faded = e.fadeEnd >= 0 && blockEnd >= e.fadeEnd;

        if (finished || faded)
            e.inUse = false;
        else
            ++count;
    }

    active.store (count, std::memory_order_relaxed);
    now += numSamples;
}
}

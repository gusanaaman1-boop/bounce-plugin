// Pattern model: timing warp, master grid, safety, gap, cap, effective values (spec 3-4).
#include "Harness.h"

using namespace bounce;
using test::ms;

namespace
{
double delayMs (const Schedule& s, int i)  { return (double) s.taps[(size_t) i].delaySamples * 1000.0 / s.sampleRate; }
}

TEST_CASE ("pattern", "default four taps land on 125/250/375/500 ms at 44.1 and 48 kHz")
{
    for (auto sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        PatternSettings s;
        s.sourceMs = 40.0f;
        const auto sch = computeSchedule (s, sr, 120.0);
        for (int i = 0; i < 4; ++i)
            CHECK_NEAR (delayMs (sch, i), 125.0 * (i + 1), 1000.0 / sr, "tap " + juce::String (i + 1) + " at " + juce::String (sr));
        CHECK (sch.safetyShiftSamples == 0, "no safety shift for a 40 ms source");
        CHECK (! sch.gapPushed, "no gap push");
        for (int i = 4; i < 8; ++i)
            CHECK (! sch.taps[(size_t) i].active, "slot beyond REPEATS is inactive");
    }
}

TEST_CASE ("pattern", "MOTION: +100 shrinks successive gaps, -100 widens them, the last stays at T")
{
    for (auto motion : { 100.0f, 50.0f, -50.0f, -100.0f })
    {
        PatternSettings s;
        s.sourceMs = 20.0f;
        s.motion = motion;
        s.repeats = 6;
        const auto sch = computeSchedule (s, 48000.0, 120.0);
        CHECK_NEAR (sch.taps[5].nominalSamples / 48.0, 750.0, 0.03, "last tap nominally at T for motion " + juce::String (motion));
        CHECK_NEAR (delayMs (sch, 5), 750.0 + sch.safetyShiftSamples / 48.0, 0.03, "actual = nominal + common safety shift");

        bool monotonic = true;
        for (int i = 1; i < 5; ++i)
        {
            const auto g0 = delayMs (sch, i) - delayMs (sch, i - 1);
            const auto g1 = delayMs (sch, i + 1) - delayMs (sch, i);
            monotonic = monotonic && (motion > 0 ? g1 < g0 : g1 > g0);
        }
        CHECK (monotonic, "gaps " + juce::String (motion > 0 ? "decrease" : "increase") + " at motion " + juce::String (motion));
    }

    // Exact formula at +100: t = T * u^0.5.
    PatternSettings s;
    s.motion = 100.0f;
    s.sourceMs = 20.0f;
    const auto sch = computeSchedule (s, 48000.0, 120.0);
    for (int i = 0; i < 4; ++i)
        CHECK_NEAR (delayMs (sch, i), 500.0 * std::sqrt ((i + 1) / 4.0), 0.03, "accelerate formula tap " + juce::String (i + 1));
}

TEST_CASE ("pattern", "master grid: REPEATS 4 -> 8 -> 4 never rewrites stored slots")
{
    PatternSettings s;
    s.sourceMs = 40.0f;
    s.time[5] = 0.70f;            // a custom dormant slot 6

    s.repeats = 8;
    auto sch = computeSchedule (s, 48000.0, 120.0);
    CHECK_NEAR (delayMs (sch, 0), 125.0, 0.03, "N=8 slot 1 still at one interval");
    CHECK_NEAR (delayMs (sch, 5), 0.70 * 8 * 125.0, 0.03, "N=8 custom slot 6 at its master-grid time");
    CHECK_NEAR (delayMs (sch, 7), 1000.0, 0.03, "N=8 slot 8 at 8 intervals");

    s.repeats = 4;
    sch = computeSchedule (s, 48000.0, 120.0);
    for (int i = 0; i < 4; ++i)
        CHECK_NEAR (delayMs (sch, i), 125.0 * (i + 1), 0.03, "back at N=4 tap " + juce::String (i + 1));
    CHECK (s.time[5] == 0.70f, "custom dormant value untouched");
}

TEST_CASE ("pattern", "source safety shifts every tap by the same amount")
{
    PatternSettings s;
    s.sourceMs = 180.0f;
    const auto sch = computeSchedule (s, 48000.0, 120.0);
    CHECK_NEAR (delayMs (sch, 0), 185.0, 0.03, "first tap = source + 5 ms");
    CHECK_NEAR (sch.safetyShiftSamples / 48.0, 60.0, 0.03, "common shift 60 ms");
    for (int i = 1; i < 4; ++i)
        CHECK_NEAR (delayMs (sch, i) - delayMs (sch, i - 1), 125.0, 0.03, "gap kept after shift, tap " + juce::String (i + 1));
    CHECK (sch.firstDelaySamples - sch.preRollSamples >= sch.sourceSamples - sch.preRollSamples, "capture complete before first tap starts");
}

TEST_CASE ("pattern", "12 ms minimum gap pushes only later taps")
{
    PatternSettings s;
    s.sourceMs = 20.0f;
    s.repeats = 8;
    s.time[6] = 0.995f;          // slot 7 placed 5 ms before slot 8
    const auto sch = computeSchedule (s, 48000.0, 120.0);
    bool ok = true;
    for (int i = 1; i < 8; ++i)
        ok = ok && (sch.taps[(size_t) i].delaySamples - sch.taps[(size_t) i - 1].delaySamples >= ms (12.0, 48000.0));
    CHECK (ok, "every adjacent gap >= 12 ms");
    CHECK (sch.gapPushed, "gap push flagged");
}

TEST_CASE ("pattern", "16 s cap scales the whole set; the maximum schedule fits the 20 s tail")
{
    PatternSettings s;
    s.repeats = 2;
    s.division = 0;              // 1/4
    s.motion = -100.0f;
    s.time = { 0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    s.time[7] = 1.0f;
    s.time[1] = 1.0f;
    const auto sch = computeSchedule (s, 48000.0, 20.0);
    CHECK (sch.capScale < 1.0, "cap engaged");
    CHECK (sch.taps[1].nominalSamples <= 16.0 * 48000.0 + 1.0, "latest nominal tap within 16 s");

    // Brute force over extreme settings: every schedule ends inside the 20 s tail.
    juce::Random r (7);
    double worst = 0.0;
    for (int k = 0; k < 4000; ++k)
    {
        PatternSettings x;
        x.repeats = 2 + r.nextInt (7);
        x.sync = r.nextBool();
        x.division = r.nextInt (8);
        x.freeMs = 30.0f + r.nextFloat() * 970.0f;
        x.motion = r.nextBool() ? -100.0f : r.nextFloat() * 200.0f - 100.0f;
        x.sourceMs = r.nextBool() ? 1000.0f : 20.0f + r.nextFloat() * 980.0f;
        for (int i = 0; i < 8; ++i)
        {
            x.time[(size_t) i] = r.nextFloat();
            x.pitchSt[(size_t) i] = r.nextBool() ? -12.0f : r.nextFloat() * 24.0f - 12.0f;
            x.reverse[(size_t) i] = r.nextBool();
        }
        x.pitchPathSt = -12.0f;
        const auto sch2 = computeSchedule (x, 48000.0, 10.0 + r.nextFloat() * 290.0);
        worst = std::max (worst, (double) sch2.endSamples / 48000.0);
    }
    test::note ("worst-case schedule end over 4000 extreme settings (REVERSE included): " + juce::String (worst, 3) + " s (tail 24 s)");
    CHECK (worst < BounceProcessor::tailSeconds, "maximum pattern + -12 st 1000 ms source + reverse swell < reported tail");
}

TEST_CASE ("pattern", "effective level and pitch follow the stated formulas")
{
    PatternSettings s;
    s.repeats = 5;
    s.decayDb = -20.0f;
    s.pitchPathSt = 8.0f;
    s.levelDb = { 0.0f, -6.0f, 3.0f, 0.0f, 0.0f, 0, 0, 0 };
    s.pitchSt = { 0.0f, 1.0f, 0.0f, 10.0f, 0.0f, 0, 0, 0 };
    s.on[2] = false;
    const auto sch = computeSchedule (s, 48000.0, 120.0);

    for (int i = 0; i < 5; ++i)
    {
        const auto q = i / 4.0;
        CHECK_NEAR (sch.taps[(size_t) i].levelDb, s.levelDb[(size_t) i] - 20.0 * q, 1.0e-4, "level tap " + juce::String (i + 1));
        const auto pitch = std::clamp (s.pitchSt[(size_t) i] + 8.0 * q, -12.0, 12.0);
        CHECK_NEAR (sch.taps[(size_t) i].pitchSt, pitch, 1.0e-4, "pitch tap " + juce::String (i + 1));
        CHECK_NEAR (sch.taps[(size_t) i].rate, std::pow (2.0, pitch / 12.0), 1.0e-9, "rate tap " + juce::String (i + 1));
    }

    CHECK (sch.taps[3].pitchSt == 12.0f, "pitch clamps to +12 (10 + 6)");
    CHECK (sch.taps[2].gain == 0.0f && ! sch.taps[2].on, "muted slot silent");
    CHECK (sch.taps[3].delaySamples == ms (500.0, 48000.0), "muting slot 3 does not move slot 4");
    CHECK_NEAR (sch.taps[1].gain, std::pow (10.0, (-6.0 - 5.0) / 20.0), 1.0e-6, "linear gain after the dB sum");
}

TEST_CASE ("pattern", "tap-time repair is deterministic and keeps valid edits")
{
    std::array<float, 8> valid { 0.1f, 0.2f, 0.33f, 0.5f, 0.51f, 0.8f, 0.9f, 1.0f };
    auto copy = valid;
    CHECK (! repairTapTimes (copy) && copy == valid, "valid set untouched");

    std::array<float, 8> bad { 0.5f, 0.2f, std::numeric_limits<float>::quiet_NaN(), -3.0f, 0.6f, 9.0f, 0.95f, 0.1f };
    auto a = bad, b = bad;
    CHECK (repairTapTimes (a), "invalid set repaired");
    repairTapTimes (b);
    CHECK (a == b, "repair deterministic");

    bool ordered = true;
    for (int i = 1; i < 8; ++i)
        ordered = ordered && a[(size_t) i] > a[(size_t) i - 1];
    CHECK (ordered && a[0] >= tapTimeMin && a[7] <= tapTimeMax, "strictly increasing, in range");
    CHECK (a[0] == 0.5f && a[4] == 0.6f, "valid edits preserved");
}

TEST_CASE ("pattern", "editing inverse: master time for an on-screen time round-trips the warp")
{
    for (auto motion : { -100.0f, -30.0f, 0.0f, 60.0f, 100.0f })
    {
        PatternSettings s;
        s.motion = motion;
        s.sourceMs = 20.0f;
        s.repeats = 5;
        s.time[2] = 0.31f;
        const auto sch = computeSchedule (s, 48000.0, 128.0);
        const auto v = masterTimeForNominal (sch.taps[2].nominalSamples, sch.phraseSamples, sch.warpPower, 5);
        CHECK_NEAR (v, 0.31, 1.0e-6, "inverse warp at motion " + juce::String (motion));
    }
}

TEST_CASE ("pattern", "tempo and free mode set the interval")
{
    PatternSettings s;
    s.sourceMs = 20.0f;
    s.division = 3;               // 1/8 triplet
    auto sch = computeSchedule (s, 48000.0, 90.0);
    CHECK_NEAR (sch.intervalSamples, 48000.0 * 60.0 / 90.0 / 3.0, 1.0e-6, "1/8 triplet at 90 BPM");

    sch = computeSchedule (s, 48000.0, 0.0);
    CHECK (sch.bpm == fallbackBpm, "missing BPM uses 120");

    s.sync = false;
    s.freeMs = 333.0f;
    sch = computeSchedule (s, 48000.0, 90.0);
    CHECK_NEAR (sch.intervalSamples, 48000.0 * 0.333, 1.0e-3, "free 333 ms");
}

TEST_CASE ("pattern", "REVERSE: the swell ends on the tap's time and the capture is ready before it starts")
{
    PatternSettings s;                 // 100 ms source, taps at 125/250/375/500 ms
    s.reverse[0] = true;
    const auto sch = computeSchedule (s, 48000.0, 120.0);
    const auto& tap = sch.taps[0];
    CHECK (tap.reverse && tap.swellSamples == sch.sourceSamples - 1 - sch.preRollSamples, "swell = the capture after the onset");
    const auto readStart = tap.delaySamples - tap.swellSamples;                     // relative to the hit
    const auto captureEnd = sch.sourceSamples - sch.preRollSamples;
    CHECK (readStart >= captureEnd + ms (5.0, 48000.0), "the reversed tap starts reading only after the capture is complete");
    CHECK (sch.safetyShiftSamples > 0, "a 100 ms reversed first repeat needs a shift at 1/16");
    for (int i = 1; i < 4; ++i)
        CHECK (sch.taps[(size_t) i].delaySamples - sch.taps[(size_t) i - 1].delaySamples == 6000, "pattern shape kept, gap " + juce::String (i));

    PatternSettings t;
    t.tightMs = 20.0f;                 // a short capture: the swell fits without moving anything
    t.reverse[2] = true;
    const auto sch2 = computeSchedule (t, 48000.0, 120.0);
    CHECK (sch2.safetyShiftSamples == 0 && sch2.taps[2].delaySamples == 18000, "TIGHT reverse on tap 3 stays on its grid time");
}

TEST_CASE ("pattern", "QUANTIZE offset: every tap moves together; too early pushes by whole intervals")
{
    PatternSettings s;
    s.sourceMs = 40.0f;
    auto a = computeSchedule (s, 48000.0, 120.0);
    applyGridOffset (a, 1200.0);       // the hit was 25 ms early: taps move 25 ms later
    for (int i = 0; i < 4; ++i)
        CHECK (a.taps[(size_t) i].delaySamples == 6000 * (i + 1) + 1200, "tap " + juce::String (i + 1) + " moved by the offset");

    auto b = computeSchedule (s, 48000.0, 120.0);
    applyGridOffset (b, -5000.0);      // a late hit: the first repeat would come before the capture ends
    CHECK (b.taps[0].delaySamples >= b.sourceSamples + ms (5.0, 48000.0), "still safe");
    CHECK ((b.taps[0].delaySamples - (6000 - 5000)) % 6000 == 0, "pushed by whole 1/16 intervals - still on the grid");
    CHECK (b.taps[1].delaySamples - b.taps[0].delaySamples == 6000, "shape kept");
}

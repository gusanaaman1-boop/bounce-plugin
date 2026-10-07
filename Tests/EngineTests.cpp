// Engine through the whole processor: timing, safety, pitch, gain, transparency, host
// variation, stress (spec 11 items 1-6 and 8).
#include "Harness.h"

using namespace bounce;
using test::ms;
using test::Rig;

namespace
{
/** Peak |x| in [from, to). */
float peakIn (const float* x, int from, int to)
{
    float p = 0.0f;
    for (int i = std::max (0, from); i < to; ++i)
        p = std::max (p, std::abs (x[i]));
    return p;
}

/** Frequency from positive-going zero crossings (linear interpolation) in [from, to). */
double zeroCrossingFrequency (const float* x, int from, int to, double sr)
{
    double first = -1.0, last = -1.0;
    int count = 0;
    for (int i = from + 1; i < to; ++i)
    {
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            const auto t = (i - 1) + x[i - 1] / (x[i - 1] - x[i]);
            if (first < 0.0) first = t; else ++count;
            last = t;
        }
    }
    return count > 0 ? sr * count / (last - first) : 0.0;
}

/** Phase of x at frequency f over [from, to), radians. */
double phaseAt (const float* x, int from, int to, double f, double sr)
{
    double re = 0.0, im = 0.0;
    for (int i = from; i < to; ++i)
    {
        const auto w = juce::MathConstants<double>::twoPi * f * i / sr;
        re += x[i] * std::cos (w);
        im -= x[i] * std::sin (w);
    }
    return std::atan2 (im, re);
}

/** Last index where |x| > level. */
int lastAbove (const float* x, int n, float level)
{
    for (int i = n - 1; i >= 0; --i)
        if (std::abs (x[i]) > level)
            return i;
    return -1;
}
}

TEST_CASE ("engine", "one impulse -> wet repeats at 125/250/375/500 ms (44.1 and 48 kHz)")
{
    for (auto sr : { 44100.0, 48000.0 })
    {
        Rig rig (sr);
        rig.set (id::sourceMs, 40.0f);
        rig.wetOnly();

        const int hit = 1000;
        auto buf = test::silence (2, (int) sr);
        buf.setSample (0, hit, 1.0f);
        buf.setSample (1, hit, 1.0f);
        rig.process (buf);

        CHECK (rig.proc.getEngine().lastTriggerClock() >= 0, "the impulse triggered");
        const auto found = test::onsets (buf.getReadPointer (0), buf.getNumSamples(), 0.05f, ms (50.0, sr));
        CHECK (found.size() == 4, "four wet onsets, got " + juce::String ((int) found.size()));

        for (size_t i = 0; i < std::min<size_t> (4, found.size()); ++i)
        {
            const auto want = hit + ms (125.0 * (double) (i + 1), sr);
            CHECK (std::abs (found[i] - want) <= 1, "repeat " + juce::String ((int) i + 1) + " at sample " + juce::String (found[i])
                                   + ", want " + juce::String (want) + " (+/-1 sample rounding at 44.1 kHz, detector tolerance 0)");
            const auto gainDb = -18.0 * (double) i / 3.0;
            CHECK_NEAR (buf.getSample (0, found[i]), std::pow (10.0, gainDb / 20.0), 1.0e-5,
                        "repeat " + juce::String ((int) i + 1) + " level follows DECAY");
        }
        CHECK (test::allFinite (buf), "finite output");
    }
}

TEST_CASE ("engine", "MOTION +/-100 moves the audible repeats; the last stays at 500 ms")
{
    for (auto motion : { 100.0f, -100.0f })
    {
        Rig rig;
        rig.set (id::sourceMs, 20.0f);     // short enough that -100 needs no safety shift
        rig.set (id::motion, motion);
        rig.set (id::decayDb, 0.0f);
        rig.wetOnly();

        auto buf = test::silence (2, 48000);
        buf.setSample (0, 100, 1.0f);
        buf.setSample (1, 100, 1.0f);
        rig.process (buf);

        const auto found = test::onsets (buf.getReadPointer (0), buf.getNumSamples(), 0.05f, ms (13.0, 48000.0));
        CHECK (found.size() == 4, "four onsets at motion " + juce::String (motion));
        if (found.size() == 4)
        {
            const auto p = std::pow (2.0, -motion / 100.0);
            for (int i = 0; i < 4; ++i)
                CHECK (found[(size_t) i] - 100 == (int) std::llround (24000.0 * std::pow ((i + 1) / 4.0, p)),
                       "tap " + juce::String (i + 1) + " at the warped time, motion " + juce::String (motion));
            const auto g1 = found[1] - found[0], g2 = found[2] - found[1], g3 = found[3] - found[2];
            CHECK (motion > 0 ? (g1 > g2 && g2 > g3) : (g1 < g2 && g2 < g3), "gap direction at motion " + juce::String (motion));
            CHECK (found[3] - 100 == 24000, "last repeat at exactly 500 ms");
        }
    }
}

TEST_CASE ("engine", "source safety: a 180 ms capture is complete before its first repeat")
{
    Rig rig;
    rig.set (id::sourceMs, 180.0f);
    rig.set (id::retriggerMs, 500.0f);
    rig.set (id::decayDb, 0.0f);
    rig.wetOnly();

    const int hit = 2000;
    auto buf = test::silence (2, 48000);
    buf.setSample (0, hit, 1.0f);
    buf.setSample (1, hit, 1.0f);
    // A quiet marker 165 ms after the hit, below the threshold: must be inside every repeat.
    const int marker = hit + ms (165.0, 48000.0);
    buf.setSample (0, marker, 0.03f);
    buf.setSample (1, marker, 0.03f);
    rig.process (buf);

    const auto& sch = rig.proc.getEngine().lastSchedule();
    CHECK_NEAR (sch.firstDelaySamples / 48.0, 185.0, 0.03, "first repeat moved to 185 ms");
    for (int i = 0; i < 4; ++i)
    {
        const auto at = hit + (int) sch.taps[(size_t) i].delaySamples;
        CHECK_NEAR (buf.getSample (0, at), 1.0, 1.0e-6, "repeat " + juce::String (i + 1) + " hit present");
        CHECK_NEAR (buf.getSample (0, at + ms (165.0, 48000.0)), 0.03, 1.0e-6,
                    "repeat " + juce::String (i + 1) + " carries the late part of the capture (no cut-off)");
        if (i > 0)
            CHECK (sch.taps[(size_t) i].delaySamples - sch.taps[(size_t) i - 1].delaySamples == 6000, "relative gaps kept");
    }
}

TEST_CASE ("engine", "pitch: -12 / 0 / +12 st halve, keep and double frequency; length scales inversely; stereo phase kept")
{
    for (auto pitch : { -12.0f, 0.0f, 12.0f })
    {
        Rig rig;
        rig.set (id::repeats, 2.0f);
        rig.set (id::tapOn (2), 0.0f);
        rig.set (id::decayDb, 0.0f);
        rig.set (id::sourceMs, 80.0f);
        rig.set (id::tapPitchSt (1), pitch);
        rig.set (id::division, 2.0f);          // 1/8 -> first repeat at 250 ms
        rig.wetOnly();

        const double f = 1000.0, sr = 48000.0;
        const int start = 4800, len = ms (60.0, sr);
        auto buf = test::silence (2, 48000);
        for (int i = 0; i < len; ++i)
        {
            const auto w = juce::MathConstants<double>::twoPi * f * i / sr;
            buf.setSample (0, start + i, (float) (0.5 * std::sin (w)));
            buf.setSample (1, start + i, (float) (0.5 * std::cos (w)));
        }
        rig.process (buf);

        const auto onset = (int) rig.proc.getEngine().lastTriggerClock() - (int) (rig.proc.getEngine().clock() - 48000);
        const auto& sch = rig.proc.getEngine().lastSchedule();
        const auto tapStart = onset + (int) sch.taps[0].delaySamples - (int) sch.preRollSamples;
        const auto rate = std::pow (2.0, pitch / 12.0);
        const auto burstFrom = tapStart + (int) ((start - (onset - sch.preRollSamples)) / rate);
        const auto burstTo = burstFrom + (int) (len / rate);
        const auto margin = (int) (5 * 48 / rate);

        const auto* L = buf.getReadPointer (0);
        const auto* R = buf.getReadPointer (1);
        const auto measured = zeroCrossingFrequency (L, burstFrom + margin, burstTo - margin, sr);
        CHECK_NEAR (measured, f * rate, f * rate * 0.002, "frequency at " + juce::String (pitch) + " st (0.2 % tolerance)");

        const auto end = lastAbove (L, buf.getNumSamples(), 0.01f);
        CHECK_NEAR (end - burstFrom, len / rate, 3.0 / rate + 2, "burst length scales by 1/rate at " + juce::String (pitch) + " st");

        const auto dphi = phaseAt (L, burstFrom + margin, burstTo - margin, f * rate, sr)
                        - phaseAt (R, burstFrom + margin, burstTo - margin, f * rate, sr);
        auto wrapped = std::remainder (dphi, juce::MathConstants<double>::twoPi);
        CHECK_NEAR (std::abs (wrapped), juce::MathConstants<double>::halfPi, 0.01, "L/R keep their 90 degree relation at " + juce::String (pitch) + " st");
        CHECK_NEAR (peakIn (L, burstFrom + margin, burstTo - margin), 0.5, 0.01, "unity passband gain at " + juce::String (pitch) + " st");
    }
}

TEST_CASE ("engine", "pitch-up is band-limited: a 15 kHz tone at +12 st does not alias back into the audio band")
{
    Rig rig;
    rig.set (id::repeats, 2.0f);
    rig.set (id::tapOn (2), 0.0f);
    rig.set (id::decayDb, 0.0f);
    rig.set (id::sourceMs, 80.0f);
    rig.set (id::tapPitchSt (1), 12.0f);
    rig.set (id::division, 2.0f);
    rig.wetOnly();

    auto buf = test::silence (2, 48000);
    buf.setSample (0, 4800, 1.0f);              // trigger
    buf.setSample (1, 4800, 1.0f);
    for (int i = 4800 + 480; i < 4800 + ms (70.0, 48000.0); ++i)
        for (int ch = 0; ch < 2; ++ch)
            buf.setSample (ch, i, (float) (0.5 * std::sin (juce::MathConstants<double>::twoPi * 15000.0 * i / 48000.0)));
    rig.process (buf);

    // At 2x, 15 kHz would read as 30 kHz - above Nyquist - and alias to 18 kHz unfiltered.
    const auto& sch = rig.proc.getEngine().lastSchedule();
    const auto tapStart = 4800 + (int) sch.taps[0].delaySamples - (int) sch.preRollSamples;
    const auto from = tapStart + (int) ((480 + sch.preRollSamples) / 2.0) + 200;
    const auto rms = [&] (int a, int b) { double e = 0; for (int i = a; i < b; ++i) e += buf.getSample (0, i) * buf.getSample (0, i); return std::sqrt (e / (b - a)); };
    const auto level = rms (from, from + 800);
    test::note ("residual of an octave-up 15 kHz tone: " + juce::String (juce::Decibels::gainToDecibels (level / 0.3536), 1) + " dB re input");
    CHECK (level < 0.3536 * 0.01, "aliased image at least 40 dB down");
}

TEST_CASE ("engine", "gain trim -6 dB = 0.501; muting removes only its tap")
{
    Rig rig;
    rig.set (id::repeats, 3.0f);
    rig.set (id::decayDb, 0.0f);
    rig.set (id::sourceMs, 30.0f);
    rig.set (id::tapLevelDb (2), -6.0f);
    rig.wetOnly();

    auto buf = test::silence (2, 48000);
    buf.setSample (0, 500, 1.0f);
    buf.setSample (1, 500, 1.0f);
    rig.process (buf);
    const auto t1 = buf.getSample (0, 500 + 6000), t2 = buf.getSample (0, 500 + 12000), t3 = buf.getSample (0, 500 + 18000);
    CHECK_NEAR (t2 / t1, 0.501187, 1.0e-5, "-6 dB trim relative to untrimmed tap");
    CHECK_NEAR (t3, 1.0, 1.0e-6, "tap 3 untrimmed");

    Rig muted;
    muted.set (id::repeats, 3.0f);
    muted.set (id::decayDb, 0.0f);
    muted.set (id::sourceMs, 30.0f);
    muted.set (id::tapOn (2), 0.0f);
    muted.wetOnly();
    auto b2 = test::silence (2, 48000);
    b2.setSample (0, 500, 1.0f);
    b2.setSample (1, 500, 1.0f);
    muted.process (b2);
    CHECK (b2.getSample (0, 500 + 6000) == 1.0f && b2.getSample (0, 500 + 18000) == 1.0f, "taps 1 and 3 unchanged");
    CHECK (peakIn (b2.getReadPointer (0), 500 + 12000 - 480, 500 + 12000 + 1440) == 0.0f, "tap 2 region silent");
}

TEST_CASE ("engine", "transparency: AMOUNT 0 is sample-identical, mono and stereo, while triggering")
{
    for (int channels : { 1, 2 })
    {
        Rig rig (48000.0, 256, channels);
        rig.set (id::amount, 0.0f);
        rig.settle();

        auto in = test::silence (channels, 96000);
        juce::Random r (3);
        for (int k = 0; k < 12; ++k)
            test::addKick (in, 2000 + k * 7000, 48000.0, 0.9f, &r);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < in.getNumSamples(); ++i)
                in.addSample (ch, i, (r.nextFloat() - 0.5f) * 1.0e-3f);   // non-zero everywhere

        auto out = in;
        rig.process (out);
        CHECK (test::identical (in, out), "bit-identical with " + juce::String (channels) + " channel(s)");
        CHECK (rig.proc.getEngine().triggerCount() >= 12, "events really triggered (" + juce::String ((int) rig.proc.getEngine().triggerCount()) + ")");
    }

    // enabled = off: identical once its 5 ms fade has completed.
    Rig rig;
    auto in = test::silence (2, 48000);
    for (int k = 0; k < 6; ++k)
        test::addKick (in, 1000 + k * 7000, 48000.0);
    auto out = in;
    rig.process (out);                 // audible repeats running...
    auto in2 = in;
    auto out2 = in2;
    rig.set (id::enabled, 0.0f);
    rig.process (out2);
    const auto fade = ms (5.0, 48000.0) + 512;
    bool same = true;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = fade; i < in2.getNumSamples(); ++i)
            same = same && out2.getSample (ch, i) == in2.getSample (ch, i);
    CHECK (same, "enabled=off passes the input unchanged after the fade");

    // Silence in, silence out.
    Rig quiet;
    quiet.set (id::amount, 100.0f);
    quiet.settle();
    auto z = test::silence (2, 48000);
    quiet.process (z);
    CHECK (z.getMagnitude (0, z.getNumSamples()) == 0.0f && test::allFinite (z), "zero input -> zero, finite output");
}

TEST_CASE ("engine", "host variation: no playhead, stop, seek, loop jump, tempo change")
{
    // No playhead at all: the local clock and 120 BPM fallback keep it working.
    {
        Rig rig;
        rig.head.provide = false;
        rig.set (id::sourceMs, 30.0f);
        rig.wetOnly();
        auto buf = test::silence (2, 48000);
        buf.setSample (0, 100, 1.0f);
        rig.process (buf);
        CHECK (! rig.proc.bpmFromHost() && rig.proc.lastBpm() == 120.0, "fallback BPM flagged");
        CHECK (buf.getSample (0, 100 + 6000) != 0.0f, "repeats still play with no host position");
    }

    // Stop cancels sounding repeats within the short fade.
    for (int mode = 0; mode < 3; ++mode)
    {
        Rig rig;
        rig.set (id::sourceMs, 30.0f);
        rig.set (id::decayDb, 0.0f);
        rig.wetOnly();
        auto a = test::silence (2, 4800);
        a.setSample (0, 100, 1.0f);
        rig.process (a);                                         // hit at 100, repeats due later

        if (mode == 0) rig.head.playing = false;                  // stop
        if (mode == 1) rig.head.sample += 480000;                 // seek
        if (mode == 2) rig.head.sample = 0;                       // loop jump back

        auto b = test::silence (2, 48000);
        rig.process (b);
        const char* what[] = { "transport stop", "seek", "loop jump" };
        CHECK (b.getMagnitude (0, ms (10.0, 48000.0) + 512, b.getNumSamples() - ms (10.0, 48000.0) - 512) == 0.0f,
               juce::String ("no repeats leak after ") + what[mode]);
    }

    // Tempo change: the running event keeps its snapshot, a new hit uses the new tempo.
    {
        Rig rig;
        rig.set (id::sourceMs, 30.0f);
        rig.set (id::decayDb, 0.0f);
        rig.wetOnly();
        auto buf = test::silence (2, 96000);
        buf.setSample (0, 100, 1.0f);
        buf.setSample (0, 48100, 1.0f);
        juce::MidiBuffer midi;
        for (int pos = 0; pos < buf.getNumSamples(); pos += 512)
        {
            if (pos >= 3000) rig.head.bpm = 150.0;                // changes after the first hit
            juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, pos, std::min (512, buf.getNumSamples() - pos));
            rig.proc.processBlock (view, midi);
            rig.head.sample += view.getNumSamples();
        }
        const auto* x = buf.getReadPointer (0);
        CHECK (x[100 + 24000] == 1.0f, "first event keeps 120 BPM (last repeat at 500 ms)");
        CHECK (x[48100 + 19200] == 1.0f, "second event uses 150 BPM (last repeat at 400 ms)");
    }
}

TEST_CASE ("engine", "determinism: any block size, repeated offline renders, larger-than-prepared blocks")
{
    auto makeInput = []
    {
        auto in = test::silence (2, 144000);
        juce::Random r (11);
        for (int k = 0; k < 14; ++k)
            test::addKick (in, 1500 + k * 9800 + r.nextInt (900), 48000.0, 0.8f, &r);
        test::addStab (in, 70000, 48000.0);
        return in;
    };

    auto render = [&] (int prepared, std::vector<int> sizes)
    {
        Rig rig (48000.0, prepared);
        rig.proc.loadPreset (4);     // Pitch Ladder Up: pitched taps exercise the resampler
        rig.settle();
        auto buf = makeInput();
        rig.process (buf, sizes);
        return buf;
    };

    const auto ref = render (512, { 512 });
    CHECK (test::identical (ref, render (512, { 512 })), "repeated offline render bit-identical");
    CHECK (test::identical (ref, render (512, { 1, 7, 64, 333, 512, 128, 29 })), "irregular block sizes bit-identical");
    CHECK (test::identical (ref, render (256, { 4096, 1000, 2000 })), "blocks larger than prepared: bounded chunks, bit-identical");
    CHECK (test::allFinite (ref), "finite");
}

TEST_CASE ("engine", "stress: 192 kHz, 8 pitched taps, 16 events full; the 17th is dropped")
{
    const double sr = 192000.0;
    Rig rig (sr, 512);
    rig.set (id::repeats, 8.0f);
    rig.set (id::sourceMs, 1000.0f);
    rig.set (id::retriggerMs, 20.0f);
    rig.set (id::division, 0.0f);
    for (int i = 1; i <= 8; ++i)
        rig.set (id::tapPitchSt (i), i % 2 ? 12.0f : -12.0f);
    rig.set (id::amount, 100.0f);
    rig.settle();

    auto buf = test::silence (2, (int) (sr * 6.0));
    for (int k = 0; k < 20; ++k)
    {
        buf.setSample (0, 1000 + k * ms (30.0, sr), 1.0f);
        buf.setSample (1, 1000 + k * ms (30.0, sr), -1.0f);
    }
    // Continuous material so every tap reads real audio.
    juce::Random r (5);
    for (int i = 0; i < buf.getNumSamples(); ++i)
        for (int ch = 0; ch < 2; ++ch)
            buf.addSample (ch, i, (r.nextFloat() - 0.5f) * 0.01f);

    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    rig.process (buf);
    const auto elapsed = juce::Time::getMillisecondCounterHiRes() - t0;

    const auto& e = rig.proc.getEngine();
    CHECK (e.triggerCount() == 20, "20 triggers detected (" + juce::String ((int) e.triggerCount()) + ")");
    CHECK (e.droppedCount() == 4, "events 17-20 dropped, got " + juce::String ((int) e.droppedCount()));
    CHECK (test::allFinite (buf), "finite under stress");
    test::note ("dense worst case at 192 kHz (16 events x 8 taps, +/-12 st): " + juce::String (elapsed / 6000.0 * 100.0, 1)
                + " % of real time on this machine (Release)");
}

TEST_CASE ("engine", "profile: normal use at 44.1 / 48 kHz")
{
    for (auto sr : { 44100.0, 48000.0 })
    {
        Rig rig (sr, 256);
        rig.proc.loadPreset (1);     // Stab Cascade, pitched
        rig.set (id::amount, 50.0f);
        rig.settle();
        auto buf = test::silence (2, (int) (sr * 20.0));
        for (int k = 0; k < 80; ++k)
            test::addKick (buf, (int) (k * sr * 0.25), sr);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        rig.process (buf);
        const auto elapsed = juce::Time::getMillisecondCounterHiRes() - t0;
        test::note ("16th-note kicks, Stab Cascade, " + juce::String (sr / 1000.0, 1) + " kHz: "
                    + juce::String (elapsed / 20000.0 * 100.0, 2) + " % of one core");
    }
}

TEST_CASE ("engine", "detector: impulses, sustained tones, quiet signal, clipping, close hits")
{
    auto countTriggers = [] (const std::function<void (juce::AudioBuffer<float>&)>& fill, float retrig = 90.0f)
    {
        Rig rig;
        rig.set (id::retriggerMs, retrig);
        rig.settle();
        auto b = test::silence (2, 96000);
        fill (b);
        rig.process (b);
        return (int) rig.proc.getEngine().triggerCount();
    };

    CHECK (countTriggers ([] (auto& b) { for (int k = 0; k < 8; ++k) b.setSample (0, 1000 + k * 9000, 0.5f); }) == 8, "8 impulses -> 8 triggers");
    CHECK (countTriggers ([] (auto& b) { for (int k = 0; k < 8; ++k) test::addKick (b, 1000 + k * 11000, 48000.0); }) == 8, "8 kicks -> 8 triggers");
    CHECK (countTriggers ([] (auto& b)
    {
        for (int i = 0; i < b.getNumSamples(); ++i)
            for (int ch = 0; ch < 2; ++ch)
                b.setSample (ch, i, (float) (0.5 * std::sin (juce::MathConstants<double>::twoPi * 110.0 * i / 48000.0) * std::min (1.0, i / 480.0)));
    }) <= 1, "a sustained tone triggers at most once (its start)");
    CHECK (countTriggers ([] (auto& b) { for (int k = 0; k < 8; ++k) test::addKick (b, 1000 + k * 11000, 48000.0, 0.02f); }) == 0,
           "hits below the -24 dBFS floor do not trigger");
    CHECK (countTriggers ([] (auto& b)
    {
        for (int k = 0; k < 6; ++k) test::addKick (b, 1000 + k * 14000, 48000.0, 4.0f);
        for (int i = 0; i < b.getNumSamples(); ++i)
            for (int ch = 0; ch < 2; ++ch)
                b.setSample (ch, i, juce::jlimit (-1.0f, 1.0f, b.getSample (ch, i)));
    }) == 6, "hard-clipped kicks -> one trigger each");
    CHECK (countTriggers ([] (auto& b) { b.setSample (0, 1000, 0.5f); b.setSample (0, 1000 + 2400, 0.5f); }) == 1,
           "two hits 50 ms apart with 90 ms retrigger -> one trigger");
    CHECK (countTriggers ([] (auto& b) { b.setSample (0, 1000, 0.5f); b.setSample (0, 1000 + 2400, 0.5f); }, 30.0f) == 2,
           "same hits with 30 ms retrigger -> two triggers");
    CHECK (countTriggers ([] (auto& b) { b.setSample (0, 1000, 0.5f); b.setSample (1, 1000, -0.5f); }) == 1,
           "opposite-polarity stereo hit is not cancelled");
}

TEST_CASE ("engine", "options off by default: the 1.0 sound is unchanged")
{
    Rig rig;
    CHECK (rig.get (id::choke) == 0.0f && rig.get (id::tightMs) == 0.0f, "CHOKE and TIGHT default off");
    const auto s = rig.proc.readSettings();
    const auto sch = computeSchedule (s, 48000.0, 120.0);
    CHECK (sch.tightSamples == 0 && ! sch.choke && sch.sourceSamples == ms (100.0, 48000.0), "default schedule as in 1.0");
}

TEST_CASE ("engine", "CHOKE: the previous roll plays until the new hit's first repeat, then stops")
{
    for (bool choke : { false, true })
    {
        Rig rig;
        rig.set (id::sourceMs, 30.0f);
        rig.set (id::decayDb, 0.0f);
        rig.set (id::division, 0.0f);            // 1/4: repeats at 500 ... 2000 ms
        rig.set (id::choke, choke ? 1.0f : 0.0f);
        rig.wetOnly();

        auto buf = test::silence (2, 48000 * 3);
        buf.setSample (0, 100, 1.0f);            // hit A
        buf.setSample (0, 100 + 36000, 0.5f);    // hit B at 750 ms, after A's first repeat
        rig.process (buf);

        const auto* x = buf.getReadPointer (0);
        CHECK (x[100 + 24000] == 1.0f, "A's first repeat (500 ms) plays either way");
        // A repeats at 500/1000/1500/2000 ms; B (at 750 ms) repeats from 1250 ms.
        CHECK (x[100 + 48000] == 1.0f, "A's repeat before B's first repeat is kept either way");
        const auto aThird = std::abs (x[100 + 72000]);
        if (choke)
        {
            CHECK (aThird == 0.0f, "with CHOKE, A's repeats after B's roll starts are gone");
            CHECK_NEAR (x[100 + 36000 + 24000], 0.5, 1.0e-6, "B's own repeats still play");
        }
        else
            CHECK (aThird == 1.0f, "without CHOKE, A keeps repeating under B");
    }
}

TEST_CASE ("engine", "TIGHT: each repeat is only the hit - short capture, earlier safety, faded ring")
{
    PatternSettings s;
    s.sourceMs = 300.0f;
    auto loose = computeSchedule (s, 48000.0, 120.0);
    s.tightMs = 40.0f;
    auto tight = computeSchedule (s, 48000.0, 120.0);
    CHECK (loose.safetyShiftSamples > 0, "300 ms source needs a safety shift");
    CHECK (tight.sourceSamples == ms (48.0, 48000.0), "TIGHT 40 ms captures 8 ms pre-roll + 40 ms");
    CHECK (tight.safetyShiftSamples == 0 && tight.taps[0].delaySamples == 6000, "so the repeats stay on the grid");

    Rig rig;
    rig.set (id::tightMs, 40.0f);
    rig.set (id::decayDb, 0.0f);
    rig.wetOnly();
    auto buf = test::silence (2, 48000);
    for (int i = 0; i < ms (200.0, 48000.0); ++i)      // a long, sustained hit
        buf.setSample (0, 1000 + i, (float) (0.8 * std::sin (juce::MathConstants<double>::twoPi * 200.0 * i / 48000.0)));
    rig.process (buf);
    const auto onset = (int) rig.proc.getEngine().lastTriggerClock();
    const auto first = onset + 6000;
    float tail = 0.0f, body = 0.0f;
    for (int i = first; i < first + ms (10.0, 48000.0); ++i) body = std::max (body, std::abs (buf.getSample (0, i)));
    for (int i = first + ms (41.0, 48000.0); i < first + ms (120.0, 48000.0); ++i) tail = std::max (tail, std::abs (buf.getSample (0, i)));
    CHECK (body > 0.5f, "the hit itself is kept");
    CHECK (tail == 0.0f, "nothing after 40 ms: a dry hit, not an echo of the sustain");
}

TEST_CASE ("engine", "host suspends processing mid-hit, then plays again with a hit on the first sample")
{
    Rig rig;
    rig.set (id::sourceMs, 30.0f);
    rig.wetOnly();

    for (int pass = 1; pass <= 4; ++pass)
    {
        // Play: a hit on the very first sample, then the host stops calling process() while
        // the hit is still loud (no silence ever reaches the detector).
        rig.head.playing = true;
        rig.head.sample = 0;
        auto hit = test::silence (2, 512);
        for (int i = 0; i < 512; ++i)
            hit.setSample (0, i, 0.8f * std::exp (-i / 2000.0f));
        const auto before = rig.proc.getEngine().triggerCount();
        rig.process (hit);
        CHECK (rig.proc.getEngine().triggerCount() == before + 1, "pass " + juce::String (pass) + ": the first hit after play triggers");

        // Stop: one block with transport stopped, still loud, then nothing (suspended).
        rig.head.playing = false;
        auto loud = test::silence (2, 512);
        for (int i = 0; i < 512; ++i) loud.setSample (0, i, 0.5f);
        rig.process (loud);
    }
}

TEST_CASE ("engine", "REVERSE: the hit lands on its time, its tail is heard BEFORE it")
{
    Rig rig;
    rig.set (id::repeats, 2.0f);
    rig.set (id::tapOn (2), 0.0f);
    rig.set (id::decayDb, 0.0f);
    rig.set (id::division, 2.0f);           // 1/8: tap 1 at 250 ms
    rig.set (id::sourceMs, 60.0f);
    rig.set (id::tapReverse (1), 1.0f);
    rig.wetOnly();

    const int hit = 2000, mark = ms (30.0, 48000.0);
    auto buf = test::silence (2, 48000);
    buf.setSample (0, hit, 1.0f);
    buf.setSample (1, hit, 1.0f);
    buf.setSample (0, hit + mark, 0.03f);    // a quiet event 30 ms into the capture
    buf.setSample (1, hit + mark, 0.03f);
    rig.process (buf);

    const auto& sch = rig.proc.getEngine().lastSchedule();
    const auto at = hit + (int) sch.taps[0].delaySamples;
    CHECK (sch.safetyShiftSamples == 0 && sch.taps[0].delaySamples == 12000, "no shift needed: on the 1/8 grid");
    CHECK (buf.getSample (0, at) == 1.0f, "the hit itself lands exactly on 250 ms");
    CHECK_NEAR (buf.getSample (0, at - mark), 0.03, 1.0e-7, "what came 30 ms after the hit is heard 30 ms BEFORE it");
    CHECK (buf.getSample (0, at + mark) == 0.0f, "and nothing of it after");

    // At +12 st the swell halves and the hit still lands on time.
    Rig up;
    up.set (id::repeats, 2.0f);
    up.set (id::tapOn (2), 0.0f);
    up.set (id::decayDb, 0.0f);
    up.set (id::division, 2.0f);
    up.set (id::sourceMs, 60.0f);
    up.set (id::tapReverse (1), 1.0f);
    up.set (id::tapPitchSt (1), 12.0f);
    up.wetOnly();
    auto b2 = test::silence (2, 48000);
    b2.setSample (0, hit, 1.0f);
    b2.setSample (1, hit, 1.0f);
    up.process (b2);
    const auto peakAt = [&] (int from, int to) { int best = from; for (int i = from; i < to; ++i) if (std::abs (b2.getSample (0, i)) > std::abs (b2.getSample (0, best))) best = i; return best; };
    CHECK (std::abs (peakAt (hit + 11000, hit + 13000) - (hit + 12000)) <= 1, "pitched reverse: peak on 250 ms (+/-1 sample)");
}

TEST_CASE ("engine", "QUANTIZE: repeats lock to the host grid wherever the hit lands")
{
    for (bool quantize : { false, true })
    {
        Rig rig;                               // playing at 120 BPM from sample 0: beats every 24000
        rig.set (id::sourceMs, 30.0f);
        rig.set (id::decayDb, 0.0f);
        rig.set (id::quantize, quantize ? 1.0f : 0.0f);
        rig.wetOnly();

        const int hit = 24000 * 2 + 1440;      // 30 ms late on beat 3 (and on its 1/16 line)
        auto buf = test::silence (2, 48000 * 3);
        buf.setSample (0, hit, 1.0f);
        buf.setSample (1, hit, 1.0f);
        rig.process (buf);

        const auto* x = buf.getReadPointer (0);
        if (quantize)
        {
            CHECK (x[48000 + 6000] == 1.0f && x[48000 + 24000] == 1.0f, "QUANTIZE: repeats on the grid (125 / 500 ms after the beat)");
            CHECK (x[hit + 6000] == 0.0f, "not 125 ms after the late hit");
        }
        else
            CHECK (x[hit + 6000] == 1.0f && x[hit + 24000] == 1.0f, "off: repeats follow the hit");
    }

    // No host position (standalone): QUANTIZE has nothing to lock to and changes nothing.
    Rig free;
    free.head.provide = false;
    free.set (id::sourceMs, 30.0f);
    free.set (id::decayDb, 0.0f);
    free.set (id::quantize, 1.0f);
    free.wetOnly();
    auto b = test::silence (2, 48000);
    b.setSample (0, 1700, 1.0f);
    free.process (b);
    CHECK (b.getSample (0, 1700 + 6000) == 1.0f, "no host grid: repeats follow the hit");
}

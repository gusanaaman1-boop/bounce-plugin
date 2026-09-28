#pragma once

#include "../Source/PluginProcessor.h"
#include "../Source/Presets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <vector>

/*
    A deliberately small test harness: named cases, CHECKs that print the measured value on
    failure, and a Rig that drives the whole BounceProcessor the way a host does.
*/
namespace test
{
struct Case { const char* suite; const char* name; std::function<void()> fn; };
std::vector<Case>& registry();
void check (bool ok, const juce::String& what, const char* file, int line);
void note (const juce::String& text);

struct Registrar { Registrar (const char* s, const char* n, std::function<void()> f) { registry().push_back ({ s, n, std::move (f) }); } };

#define TEST_CAT2(a, b) a##b
#define TEST_CAT(a, b) TEST_CAT2(a, b)
#define TEST_CASE(suite, name) \
    static void TEST_CAT(testFn_, __LINE__)(); \
    static test::Registrar TEST_CAT(testReg_, __LINE__) (suite, name, &TEST_CAT(testFn_, __LINE__)); \
    static void TEST_CAT(testFn_, __LINE__)()

#define CHECK(cond, what) test::check ((cond), (what), __FILE__, __LINE__)
#define CHECK_NEAR(a, b, tol, what) test::check (std::abs ((double) (a) - (double) (b)) <= (tol), \
    juce::String (what) + "  [got " + juce::String ((double) (a), 6) + ", want " + juce::String ((double) (b), 6) + " +/- " + juce::String ((double) (tol)) + "]", __FILE__, __LINE__)

struct PlayHead final : juce::AudioPlayHead
{
    bool provide = true;          // false: the host gives no position at all
    bool withBpm = true;
    bool playing = true;
    double bpm = 120.0;
    juce::int64 sample = 0;

    juce::Optional<PositionInfo> getPosition() const override
    {
        if (! provide)
            return {};

        PositionInfo info;
        info.setIsPlaying (playing);
        if (withBpm)
            info.setBpm (bpm);
        info.setTimeInSamples (sample);
        info.setPpqPosition ((double) sample / 48000.0 * bpm / 60.0);
        return info;
    }
};

struct Rig
{
    BounceProcessor proc;
    PlayHead head;
    double sr;
    int block;
    int channels;

    Rig (double sampleRate = 48000.0, int blockSize = 512, int numChannels = 2)
        : sr (sampleRate), block (blockSize), channels (numChannels)
    {
        auto layout = proc.getBusesLayout();
        const auto set = numChannels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        layout.inputBuses.getReference (0) = set;
        layout.outputBuses.getReference (0) = set;
        proc.setBusesLayout (layout);
        proc.setRateAndBufferSizeDetails (sr, block);
        proc.setPlayHead (&head);
        proc.prepareToPlay (sr, block);
    }

    void set (const juce::String& paramID, float value)
    {
        auto* p = proc.apvts.getParameter (paramID);
        jassert (p != nullptr);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    /** Re-prepares so the 5 ms smoothers start at the current parameter values. */
    void settle()  { proc.prepareToPlay (sr, block); }

    float get (const juce::String& paramID) const  { return proc.apvts.getRawParameterValue (paramID)->load(); }

    /** Wet-only, AMOUNT 100 %: output is exactly the wet signal. */
    void wetOnly()
    {
        set (bounce::id::amount, 100.0f);
        set (bounce::id::wetOnly, 1.0f);
        settle();
    }

    /** Processes `buffer` in place in host-sized blocks (or the given pattern of sizes). */
    void process (juce::AudioBuffer<float>& buffer, const std::vector<int>& sizes = {})
    {
        juce::MidiBuffer midi;
        int pos = 0, k = 0;

        while (pos < buffer.getNumSamples())
        {
            auto n = sizes.empty() ? block : sizes[(size_t) (k++ % (int) sizes.size())];
            n = juce::jmin (n, buffer.getNumSamples() - pos);
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), pos, n);
            proc.processBlock (view, midi);
            if (head.playing)
                head.sample += n;
            pos += n;
        }
    }
};

inline juce::AudioBuffer<float> silence (int channels, int samples)
{
    juce::AudioBuffer<float> b (channels, samples);
    b.clear();
    return b;
}

inline int ms (double milliseconds, double sr)  { return (int) std::llround (milliseconds * 0.001 * sr); }

/** A synthetic drum hit: pitched body plus a noise click. */
inline void addKick (juce::AudioBuffer<float>& b, int at, double sr, float gain = 0.9f, juce::Random* rnd = nullptr)
{
    juce::Random local (at);
    auto& r = rnd != nullptr ? *rnd : local;
    double phase = 0.0;
    for (int i = 0; at + i < b.getNumSamples() && i < (int) (0.25 * sr); ++i)
    {
        const auto t = i / sr;
        phase += juce::MathConstants<double>::twoPi * (50.0 + 90.0 * std::exp (-t * 35.0)) / sr;
        const auto v = (float) (gain * (std::sin (phase) * std::exp (-t * 12.0)
                                        + 0.3 * (r.nextFloat() * 2.0 - 1.0) * std::exp (-t * 250.0)));
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            b.addSample (ch, at + i, v);
    }
}

/** A tonal stab: a detuned saw-ish chord with a fast attack and short decay. */
inline void addStab (juce::AudioBuffer<float>& b, int at, double sr, float gain = 0.5f)
{
    const double freqs[] = { 220.0, 277.18, 329.63, 440.0 };
    for (int i = 0; at + i < b.getNumSamples() && i < (int) (0.3 * sr); ++i)
    {
        const auto t = i / sr;
        double v = 0.0;
        for (auto f : freqs)
            for (int h = 1; h <= 6; ++h)
                v += std::sin (juce::MathConstants<double>::twoPi * f * h * t) / h;
        const auto env = std::min (1.0, t / 0.002) * std::exp (-t * 9.0);
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            b.addSample (ch, at + i, (float) (gain * 0.25 * v * env));
    }
}

inline bool allFinite (const juce::AudioBuffer<float>& b)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (ch, i)))
                return false;
    return true;
}

inline bool identical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
        return false;
    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        if (std::memcmp (a.getReadPointer (ch), b.getReadPointer (ch), sizeof (float) * (size_t) a.getNumSamples()) != 0)
            return false;
    return true;
}

/** Indices where |x| rises above `level` after having been below it for `gap` samples. */
inline std::vector<int> onsets (const float* x, int n, float level, int gap)
{
    std::vector<int> out;
    int quiet = gap;
    for (int i = 0; i < n; ++i)
    {
        if (std::abs (x[i]) >= level && quiet >= gap)
            out.push_back (i);
        quiet = std::abs (x[i]) >= level ? 0 : quiet + 1;
    }
    return out;
}
}

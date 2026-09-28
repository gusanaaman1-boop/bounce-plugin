#include "Parameters.h"

namespace bounce
{
namespace id
{
juce::String tapOn (int slot)       { return "tap" + juce::String (slot) + "On"; }
juce::String tapTime (int slot)     { return "tap" + juce::String (slot) + "Time"; }
juce::String tapLevelDb (int slot)  { return "tap" + juce::String (slot) + "LevelDb"; }
juce::String tapPitchSt (int slot)  { return "tap" + juce::String (slot) + "PitchSt"; }
}

juce::StringArray divisionNames()
{
    return { "1/4", "1/8 dotted", "1/8", "1/8 triplet", "1/16 dotted", "1/16", "1/16 triplet", "1/32" };
}

namespace
{
using Attr = juce::AudioParameterFloatAttributes;

juce::ParameterID pid (const juce::String& s)  { return { s, parameterVersion }; }

juce::NormalisableRange<float> skewed (float lo, float hi, float centre, float interval = 0.0f)
{
    juce::NormalisableRange<float> r (lo, hi, interval);
    r.setSkewForCentre (centre);
    return r;
}

juce::String fmt (float v, int decimals, const char* unit)
{
    return juce::String (v, decimals) + unit;
}

juce::String signedFmt (float v, int decimals, const char* unit)
{
    return (v > 0.0f ? "+" : "") + juce::String (v, decimals) + unit;
}
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // --- Main
    layout.add (std::make_unique<juce::AudioParameterBool> (pid (id::enabled), "Enabled", true));
    layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::amount), "Amount",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 35.0f,
        Attr().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return fmt (v, 0, " %"); })));
    layout.add (std::make_unique<juce::AudioParameterBool> (pid (id::wetOnly), "Wet Only", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::outputDb), "Output",
        juce::NormalisableRange<float> (-18.0f, 12.0f, 0.1f), 0.0f,
        Attr().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return signedFmt (v, 1, " dB"); })));

    // --- Pattern
    layout.add (std::make_unique<juce::AudioParameterInt> (pid (id::repeats), "Repeats", 2, numSlots, 4));
    layout.add (std::make_unique<juce::AudioParameterBool> (pid (id::sync), "Sync", true));
    layout.add (std::make_unique<juce::AudioParameterChoice> (pid (id::division), "Division", divisionNames(), defaultDivision));
    layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::freeMs), "Free Interval",
        skewed (30.0f, 1000.0f, 200.0f, 0.1f), 125.0f,
        Attr().withLabel ("ms").withStringFromValueFunction ([] (float v, int) { return fmt (v, 0, " ms"); })));
    layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::motion), "Motion",
        juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f,
        Attr().withStringFromValueFunction ([] (float v, int) { return signedFmt (v, 0, ""); })));
    layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::decayDb), "Decay",
        juce::NormalisableRange<float> (-36.0f, 0.0f, 0.1f), -18.0f,
        Attr().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return fmt (v, 1, " dB"); })));
    layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::pitchPathSt), "Pitch Path",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
        Attr().withLabel ("st").withStringFromValueFunction ([] (float v, int) { return signedFmt (v, 1, " st"); })));

    // --- Capture
    layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::sourceMs), "Source Length",
        skewed (20.0f, 1000.0f, 150.0f, 0.1f), 100.0f,
        Attr().withLabel ("ms").withStringFromValueFunction ([] (float v, int) { return fmt (v, 0, " ms"); })));
    layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::thresholdDb), "Threshold",
        juce::NormalisableRange<float> (-48.0f, -6.0f, 0.1f), -24.0f,
        Attr().withLabel ("dBFS").withStringFromValueFunction ([] (float v, int) { return fmt (v, 1, " dBFS"); })));
    layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::retriggerMs), "Retrigger",
        skewed (20.0f, 500.0f, 120.0f, 0.1f), 90.0f,
        Attr().withLabel ("ms").withStringFromValueFunction ([] (float v, int) { return fmt (v, 0, " ms"); })));

    // --- Point bank: all eight slots are always host-visible, even beyond REPEATS.
    for (int slot = 1; slot <= numSlots; ++slot)
    {
        const auto n = juce::String (slot);

        layout.add (std::make_unique<juce::AudioParameterBool> (pid (id::tapOn (slot)), "Tap " + n + " On", true));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::tapTime (slot)), "Tap " + n + " Time",
            juce::NormalisableRange<float> (tapTimeMin, tapTimeMax), defaultTapTime (slot - 1),
            Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (v * 8.0f, 3) + " grid"; })));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::tapLevelDb (slot)), "Tap " + n + " Level",
            juce::NormalisableRange<float> (-48.0f, 6.0f, 0.1f), 0.0f,
            Attr().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return signedFmt (v, 1, " dB"); })));
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id::tapPitchSt (slot)), "Tap " + n + " Pitch",
            juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
            Attr().withLabel ("st").withStringFromValueFunction ([] (float v, int) { return signedFmt (v, 1, " st"); })));
    }

    // --- Options (1.1). Appended after the point bank so no existing parameter moves.
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::choke, parameterVersionOptions }, "Choke", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::tightMs, parameterVersionOptions }, "Tight",
        juce::NormalisableRange<float> (0.0f, 200.0f, 0.1f), 0.0f,
        Attr().withLabel ("ms").withStringFromValueFunction ([] (float v, int) { return v < 0.5f ? juce::String ("Off") : fmt (v, 0, " ms"); })));

    return layout;
}
}

#include "Presets.h"
#include "Parameters.h"

#include <array>

namespace bounce
{
float FactoryPreset::valueOf (const juce::String& parameterID, float fallback) const
{
    for (const auto& [key, value] : values)
        if (key == parameterID)
            return value;

    return fallback;
}

namespace
{
struct Spec
{
    const char* name;
    int repeats = 4;
    int division = defaultDivision;
    float motion = 0.0f;
    float decayDb = -18.0f;
    float pitchPathSt = 0.0f;
    float amount = 35.0f;
    float sourceMs = 100.0f;
    float thresholdDb = -24.0f;
    float retriggerMs = 90.0f;
    bool choke = false;
    float tightMs = 0.0f;
    bool quantize = false;
    std::array<bool, 8> reverse {};
    std::array<float, 8> levelDb {};
    std::array<float, 8> pitchSt {};
    std::array<bool, 8> on { true, true, true, true, true, true, true, true };
    std::array<float, 8> time { 0.125f, 0.25f, 0.375f, 0.5f, 0.625f, 0.75f, 0.875f, 1.0f };
};

FactoryPreset build (const Spec& s)
{
    FactoryPreset p;
    p.name = s.name;
    auto& v = p.values;

    v.emplace_back (id::enabled, 1.0f);
    v.emplace_back (id::amount, s.amount);
    v.emplace_back (id::wetOnly, 0.0f);
    v.emplace_back (id::outputDb, 0.0f);
    v.emplace_back (id::repeats, (float) s.repeats);
    v.emplace_back (id::sync, 1.0f);
    v.emplace_back (id::division, (float) s.division);
    v.emplace_back (id::freeMs, 125.0f);
    v.emplace_back (id::motion, s.motion);
    v.emplace_back (id::decayDb, s.decayDb);
    v.emplace_back (id::pitchPathSt, s.pitchPathSt);
    v.emplace_back (id::sourceMs, s.sourceMs);
    v.emplace_back (id::thresholdDb, s.thresholdDb);
    v.emplace_back (id::retriggerMs, s.retriggerMs);
    v.emplace_back (id::choke, s.choke ? 1.0f : 0.0f);
    v.emplace_back (id::tightMs, s.tightMs);
    v.emplace_back (id::quantize, s.quantize ? 1.0f : 0.0f);
    for (int i = 0; i < numSlots; ++i)
        v.emplace_back (id::tapReverse (i + 1), s.reverse[(size_t) i] ? 1.0f : 0.0f);

    for (int i = 0; i < numSlots; ++i)
    {
        v.emplace_back (id::tapOn (i + 1), s.on[(size_t) i] ? 1.0f : 0.0f);
        v.emplace_back (id::tapTime (i + 1), s.time[(size_t) i]);
        v.emplace_back (id::tapLevelDb (i + 1), s.levelDb[(size_t) i]);
        v.emplace_back (id::tapPitchSt (i + 1), s.pitchSt[(size_t) i]);
    }

    return p;
}

// Division indices: 0 1/4, 1 1/8., 2 1/8, 3 1/8T, 4 1/16., 5 1/16, 6 1/16T, 7 1/32.
std::vector<FactoryPreset> makeAll()
{
    std::vector<FactoryPreset> all;

    { Spec s { "Straight Four" };
      all.push_back (build (s)); }

    { Spec s { "Stab Cascade" };
      s.motion = 35.0f; s.pitchPathSt = 7.0f; s.sourceMs = 120.0f;
      all.push_back (build (s)); }

    { Spec s { "Accelerating Fill" };
      s.repeats = 8; s.division = 2; s.motion = 80.0f; s.decayDb = -9.0f; s.amount = 45.0f;
      s.sourceMs = 70.0f; s.thresholdDb = -22.0f;
      all.push_back (build (s)); }

    { Spec s { "Slow Falling Echo" };
      s.repeats = 5; s.division = 1; s.motion = -60.0f; s.decayDb = -30.0f; s.pitchPathSt = -5.0f;
      s.amount = 40.0f; s.sourceMs = 60.0f;
      all.push_back (build (s)); }

    { Spec s { "Pitch Ladder Up" };
      s.repeats = 6; s.decayDb = -8.0f; s.sourceMs = 90.0f;
      s.pitchSt = { 2.0f, 3.0f, 5.0f, 7.0f, 10.0f, 12.0f, 12.0f, 12.0f };
      all.push_back (build (s)); }

    { Spec s { "Pitch Ladder Down" };
      s.repeats = 6; s.decayDb = -10.0f; s.pitchPathSt = -12.0f; s.sourceMs = 90.0f;
      all.push_back (build (s)); }

    { Spec s { "Snare Run" };
      s.repeats = 8; s.division = 7; s.motion = 25.0f; s.decayDb = -12.0f; s.amount = 50.0f;
      s.sourceMs = 45.0f; s.thresholdDb = -20.0f; s.retriggerMs = 150.0f;
      all.push_back (build (s)); }

    { Spec s { "Percussion Triplets" };
      s.repeats = 6; s.division = 6; s.decayDb = -6.0f; s.sourceMs = 60.0f;
      s.levelDb = { 0.0f, -8.0f, -4.0f, -10.0f, -6.0f, -12.0f, 0.0f, 0.0f };
      s.pitchSt = { 0.0f, 0.0f, 5.0f, 0.0f, 0.0f, 7.0f, 0.0f, 0.0f };
      all.push_back (build (s)); }

    { Spec s { "Vocal Answer" };
      s.repeats = 3; s.division = 0; s.motion = -20.0f; s.decayDb = -9.0f; s.amount = 40.0f;
      s.sourceMs = 240.0f; s.thresholdDb = -30.0f; s.retriggerMs = 250.0f;
      s.pitchSt = { 0.0f, -5.0f, 7.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
      all.push_back (build (s)); }

    { Spec s { "Dark Downroll" };
      s.repeats = 8; s.motion = -45.0f; s.decayDb = -24.0f; s.pitchPathSt = -7.0f; s.amount = 40.0f;
      s.sourceMs = 50.0f;
      all.push_back (build (s)); }

    { Spec s { "Tiny Double" };
      s.repeats = 2; s.division = 7; s.decayDb = -6.0f; s.amount = 50.0f; s.sourceMs = 30.0f;
      s.levelDb = { 0.0f, -6.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
      all.push_back (build (s)); }

    { Spec s { "Pre-Drop Rush" };
      s.repeats = 8; s.division = 2; s.motion = 100.0f; s.decayDb = 0.0f; s.pitchPathSt = 12.0f;
      s.amount = 55.0f; s.sourceMs = 60.0f;
      s.levelDb = { -12.0f, -10.0f, -8.0f, -6.0f, -4.5f, -3.0f, -1.5f, 0.0f };
      all.push_back (build (s)); }

    // --- 1.1: the CHOKE / TIGHT options. Appended so the first twelve keep their indices.
    { Spec s { "Choke Roll" };
      s.repeats = 8; s.division = 7; s.motion = 40.0f; s.decayDb = -10.0f; s.amount = 50.0f;
      s.thresholdDb = -22.0f; s.choke = true; s.tightMs = 40.0f;          // 8 x 1/32 = one beat
      all.push_back (build (s)); }

    { Spec s { "Tight Ratchet" };
      s.repeats = 3; s.division = 6; s.decayDb = -6.0f; s.amount = 55.0f; s.choke = true; s.tightMs = 25.0f;   // half a beat
      s.levelDb = { 0.0f, -7.0f, -3.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
      all.push_back (build (s)); }

    { Spec s { "Bouncing Ball" };
      s.repeats = 8; s.division = 7; s.motion = 70.0f;                    // 8 x 1/32 = one beat s.decayDb = -27.0f; s.pitchPathSt = 3.0f;
      s.amount = 45.0f; s.choke = true; s.tightMs = 70.0f;
      all.push_back (build (s)); }

    // Categories of the first fifteen (their indices never change).
    const char* firstCategories[] = { "ESSENTIALS", "PITCH", "ROLLS & FILLS", "ECHO & SPACE", "PITCH", "PITCH",
                                      "ROLLS & FILLS", "GROOVE", "VOCAL & CHOPS", "ECHO & SPACE", "ESSENTIALS",
                                      "FX & DROPS", "CHOKE & TIGHT", "CHOKE & TIGHT", "CHOKE & TIGHT" };
    for (size_t i = 0; i < all.size(); ++i)
        all[i].category = firstCategories[i];

    // --- 1.2: forty more, appended. Every one is checked by the state tests: no source-safety
    // shift at 120 BPM, audibly different from every other preset, output within +6 dB of input.
    auto add = [&all] (const char* category, const Spec& s)
    {
        auto p = build (s);
        p.category = category;
        all.push_back (std::move (p));
    };

    // Division indices: 0 1/4, 1 1/8., 2 1/8, 3 1/8T, 4 1/16., 5 1/16, 6 1/16T, 7 1/32.
    // Master-grid times are in eighths of the 8-interval grid: k intervals -> k / 8.

    // ESSENTIALS - plain, useful repeats
    { Spec s { "Eighth Echo" }; s.division = 2; s.decayDb = -15.0f; add ("ESSENTIALS", s); }
    { Spec s { "Dotted Bounce" }; s.division = 1; s.decayDb = -14.0f; s.sourceMs = 120.0f; add ("ESSENTIALS", s); }
    { Spec s { "Triplet Taps" }; s.repeats = 6; s.division = 3; s.decayDb = -16.0f; s.sourceMs = 80.0f; add ("ESSENTIALS", s); }
    { Spec s { "Quarter Answer" }; s.repeats = 2; s.division = 0; s.decayDb = -6.0f; s.amount = 40.0f; s.sourceMs = 150.0f; add ("ESSENTIALS", s); }
    { Spec s { "Sixteenth Slap" }; s.repeats = 3; s.decayDb = -9.0f; s.amount = 30.0f; s.sourceMs = 60.0f; add ("ESSENTIALS", s); }

    // ROLLS & FILLS
    { Spec s { "Snare Build 8" }; s.repeats = 8; s.motion = 30.0f; s.decayDb = 0.0f; s.amount = 50.0f; s.sourceMs = 50.0f;
      s.levelDb = { -14.0f, -12.0f, -10.0f, -8.0f, -6.0f, -4.0f, -2.0f, 0.0f }; add ("ROLLS & FILLS", s); }
    { Spec s { "Tom Rush" }; s.repeats = 6; s.division = 6; s.motion = 50.0f; s.pitchPathSt = -7.0f; s.decayDb = -8.0f;
      s.amount = 45.0f; s.sourceMs = 70.0f; add ("ROLLS & FILLS", s); }
    { Spec s { "Slowing Roll" }; s.repeats = 8; s.division = 7; s.motion = -40.0f; s.decayDb = -15.0f; s.amount = 45.0f;
      s.sourceMs = 25.0f; add ("ROLLS & FILLS", s); }
    { Spec s { "Half-Time Roll" }; s.division = 2; s.motion = 20.0f; s.decayDb = -12.0f; s.amount = 40.0f; s.sourceMs = 90.0f;
      add ("ROLLS & FILLS", s); }
    { Spec s { "Buzz Roll" }; s.repeats = 8; s.division = 7; s.decayDb = -20.0f; s.amount = 45.0f; s.tightMs = 20.0f;
      s.levelDb = { 0.0f, -3.0f, 0.0f, -3.0f, 0.0f, -3.0f, 0.0f, -3.0f }; add ("ROLLS & FILLS", s); }

    // PITCH
    { Spec s { "Octave Jump" }; s.decayDb = -12.0f; s.pitchSt = { 12.0f, 0.0f, 12.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }; add ("PITCH", s); }
    { Spec s { "Fifth Stack" }; s.division = 2; s.decayDb = -10.0f; s.sourceMs = 120.0f;
      s.pitchSt = { 7.0f, 12.0f, 7.0f, 12.0f, 0.0f, 0.0f, 0.0f, 0.0f }; add ("PITCH", s); }
    { Spec s { "Pentatonic Fall" }; s.repeats = 6; s.decayDb = -9.0f; s.sourceMs = 90.0f;
      s.pitchSt = { 12.0f, 10.0f, 7.0f, 5.0f, 3.0f, 0.0f, 0.0f, 0.0f }; add ("PITCH", s); }
    { Spec s { "Wobble Tune" }; s.repeats = 8; s.division = 7; s.decayDb = -14.0f; s.amount = 40.0f; s.sourceMs = 45.0f;
      s.pitchSt = { 0.5f, -0.5f, 0.5f, -0.5f, 0.5f, -0.5f, 0.5f, -0.5f }; add ("PITCH", s); }
    { Spec s { "Dive Bomb" }; s.repeats = 6; s.motion = -30.0f; s.pitchPathSt = -12.0f; s.decayDb = -10.0f; s.sourceMs = 60.0f;
      add ("PITCH", s); }

    // ECHO & SPACE
    { Spec s { "Canyon Throw" }; s.repeats = 3; s.division = 0; s.pitchPathSt = -2.0f; s.amount = 40.0f; s.sourceMs = 200.0f;
      add ("ECHO & SPACE", s); }
    { Spec s { "Long Drift" }; s.repeats = 8; s.division = 1; s.motion = -20.0f; s.decayDb = -33.0f; s.amount = 40.0f;
      s.sourceMs = 150.0f; add ("ECHO & SPACE", s); }
    { Spec s { "Ghost Tail" }; s.repeats = 5; s.division = 2; s.decayDb = -30.0f; s.amount = 30.0f; s.sourceMs = 180.0f;
      s.levelDb = { -6.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }; add ("ECHO & SPACE", s); }
    { Spec s { "Reverse Swell" }; s.repeats = 5; s.division = 3; s.decayDb = 0.0f;
      s.levelDb = { -24.0f, -18.0f, -12.0f, -6.0f, 0.0f, 0.0f, 0.0f, 0.0f }; add ("ECHO & SPACE", s); }
    { Spec s { "Fading Stairs" }; s.repeats = 6; s.division = 4; s.decayDb = -24.0f; s.pitchPathSt = -5.0f; add ("ECHO & SPACE", s); }

    // GROOVE - swing, clave and polyrhythm placements on the master grid
    { Spec s { "Afro Shuffle" }; s.decayDb = -4.0f; s.sourceMs = 60.0f;
      s.time = { 1.33f / 8, 2.0f / 8, 3.33f / 8, 4.0f / 8, 0.625f, 0.75f, 0.875f, 1.0f };
      s.levelDb = { -6.0f, 0.0f, -8.0f, -2.0f, 0.0f, 0.0f, 0.0f, 0.0f }; add ("GROOVE", s); }
    { Spec s { "Clave Answer" }; s.division = 2; s.decayDb = -3.0f; s.sourceMs = 80.0f;
      s.time = { 1.5f / 8, 3.0f / 8, 5.0f / 8, 6.0f / 8, 6.5f / 8, 7.0f / 8, 7.5f / 8, 1.0f };
      s.levelDb = { 0.0f, -3.0f, -6.0f, -4.0f, 0.0f, 0.0f, 0.0f, 0.0f };
      s.pitchSt = { 0.0f, 2.0f, 0.0f, -3.0f, 0.0f, 0.0f, 0.0f, 0.0f }; add ("GROOVE", s); }
    { Spec s { "Shaker Swing" }; s.repeats = 8; s.decayDb = -6.0f; s.amount = 40.0f; s.tightMs = 40.0f;
      s.time = { 1.35f / 8, 2.0f / 8, 3.35f / 8, 4.0f / 8, 5.35f / 8, 6.0f / 8, 7.35f / 8, 1.0f };
      s.levelDb = { -8.0f, 0.0f, -8.0f, -1.0f, -9.0f, -2.0f, -10.0f, -3.0f }; add ("GROOVE", s); }
    { Spec s { "Conga Call" }; s.repeats = 3; s.division = 3; s.decayDb = -6.0f; s.amount = 40.0f; s.sourceMs = 90.0f;
      s.pitchSt = { 5.0f, 0.0f, -5.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }; add ("GROOVE", s); }
    { Spec s { "Offbeat Skip" }; s.division = 2; s.decayDb = -9.0f; s.sourceMs = 80.0f;
      s.time = { 0.5f / 8, 1.5f / 8, 2.5f / 8, 3.5f / 8, 0.625f, 0.75f, 0.875f, 1.0f }; add ("GROOVE", s); }
    // The hit plus two repeats split the bar into three against the 4/4 grid.
    { Spec s { "Polyrhythm 3:4" }; s.repeats = 2; s.division = 2; s.decayDb = -9.0f; s.sourceMs = 120.0f;
      s.time = { 1.0f / 3, 2.0f / 3, 0.75f, 0.8f, 0.85f, 0.9f, 0.95f, 1.0f }; add ("GROOVE", s); }

    // VOCAL & CHOPS - longer captures, higher sensitivity, slower retrigger
    { Spec s { "Chop Stutter" }; s.division = 7; s.decayDb = -6.0f; s.amount = 45.0f; s.sourceMs = 55.0f; add ("VOCAL & CHOPS", s); }
    { Spec s { "Vox Call Up" }; s.repeats = 3; s.division = 2; s.decayDb = -8.0f; s.sourceMs = 200.0f; s.thresholdDb = -30.0f;
      s.retriggerMs = 250.0f; s.pitchSt = { 5.0f, 7.0f, 12.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }; add ("VOCAL & CHOPS", s); }
    { Spec s { "Vox Throw Down" }; s.repeats = 3; s.division = 1; s.decayDb = -12.0f; s.sourceMs = 300.0f; s.thresholdDb = -30.0f;
      s.retriggerMs = 300.0f; s.pitchSt = { 0.0f, -5.0f, -12.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }; add ("VOCAL & CHOPS", s); }
    { Spec s { "Word Repeat" }; s.repeats = 2; s.division = 0; s.decayDb = -3.0f; s.amount = 45.0f; s.sourceMs = 400.0f;
      s.thresholdDb = -28.0f; s.retriggerMs = 400.0f; add ("VOCAL & CHOPS", s); }
    { Spec s { "Breath Echo" }; s.division = 0; s.motion = -30.0f; s.decayDb = -24.0f; s.amount = 30.0f; s.sourceMs = 250.0f;
      s.thresholdDb = -32.0f; s.retriggerMs = 300.0f; add ("VOCAL & CHOPS", s); }

    // CHOKE & TIGHT - rolls and ratchets rather than echoes
    { Spec s { "Hat Ratchet" }; s.division = 7; s.decayDb = -4.0f; s.amount = 50.0f; s.choke = true; s.tightMs = 15.0f;
      s.levelDb = { 0.0f, -6.0f, -3.0f, -9.0f, 0.0f, 0.0f, 0.0f, 0.0f }; add ("CHOKE & TIGHT", s); }
    { Spec s { "Tight Double" }; s.repeats = 2; s.division = 7; s.decayDb = -4.0f; s.amount = 55.0f; s.choke = true; s.tightMs = 30.0f;
      add ("CHOKE & TIGHT", s); }
    { Spec s { "Machine Gun" }; s.repeats = 8; s.division = 7; s.decayDb = -3.0f; s.amount = 50.0f; s.choke = true; s.tightMs = 20.0f;
      add ("CHOKE & TIGHT", s); }
    { Spec s { "Choke Triplets" }; s.repeats = 3; s.division = 6; s.decayDb = -8.0f; s.amount = 50.0f; s.choke = true; s.tightMs = 35.0f;
      s.pitchSt = { 0.0f, 0.0f, 12.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }; add ("CHOKE & TIGHT", s); }
    { Spec s { "Dry Clicks" }; s.repeats = 6; s.motion = 60.0f; s.decayDb = -12.0f; s.pitchPathSt = 12.0f; s.amount = 45.0f;
      s.choke = true; s.tightMs = 10.0f; add ("CHOKE & TIGHT", s); }

    // FX & DROPS
    { Spec s { "Riser Ladder" }; s.repeats = 8; s.motion = 60.0f; s.pitchPathSt = 12.0f; s.decayDb = 0.0f; s.amount = 50.0f;
      s.sourceMs = 80.0f; s.levelDb = { -15.0f, -13.0f, -11.0f, -9.0f, -7.0f, -5.0f, -2.5f, 0.0f }; add ("FX & DROPS", s); }
    { Spec s { "Drop Stutter" }; s.repeats = 8; s.division = 7; s.decayDb = -2.0f; s.amount = 60.0f; s.sourceMs = 55.0f;
      add ("FX & DROPS", s); }
    { Spec s { "Glitch Scatter" }; s.repeats = 7; s.decayDb = -6.0f; s.amount = 40.0f; s.tightMs = 35.0f;
      s.time = { 0.09f, 0.16f, 0.30f, 0.36f, 0.52f, 0.71f, 0.78f, 1.0f };
      s.pitchSt = { 12.0f, -5.0f, 0.0f, 7.0f, -12.0f, 3.0f, -7.0f, 0.0f };
      s.levelDb = { 0.0f, -6.0f, -3.0f, -9.0f, -2.0f, -8.0f, -4.0f, 0.0f }; add ("FX & DROPS", s); }
    { Spec s { "Tape Stop Fall" }; s.repeats = 6; s.motion = -40.0f; s.pitchPathSt = -12.0f; s.decayDb = -6.0f; s.sourceMs = 60.0f;
      s.pitchSt = { 0.0f, 0.0f, 0.0f, 0.0f, -2.0f, -4.0f, 0.0f, 0.0f }; add ("FX & DROPS", s); }

    // --- 1.3: REVERSE and QUANTIZE, appended (the first 55 keep their indices).
    { Spec s { "Swell Into Beat" }; s.repeats = 2; s.division = 2; s.decayDb = -3.0f;
      s.reverse = { false, true, false, false, false, false, false, false }; add ("REVERSE", s); }
    { Spec s { "Backwards Answer" }; s.repeats = 3; s.division = 2; s.decayDb = -9.0f; s.sourceMs = 120.0f;
      s.reverse = { true, true, true, false, false, false, false, false }; add ("REVERSE", s); }
    { Spec s { "Mirror Bounce" }; s.decayDb = -8.0f; s.tightMs = 30.0f;
      s.reverse = { false, true, false, true, false, false, false, false }; add ("REVERSE", s); }
    { Spec s { "Reverse Ladder" }; s.division = 2; s.decayDb = -6.0f; s.sourceMs = 80.0f;
      s.pitchSt = { 0.0f, 3.0f, 7.0f, 12.0f, 0.0f, 0.0f, 0.0f, 0.0f };
      s.reverse = { true, true, true, true, false, false, false, false }; add ("REVERSE", s); }
    { Spec s { "Suck Back Snare" }; s.repeats = 2; s.division = 0; s.decayDb = -6.0f; s.amount = 45.0f; s.sourceMs = 200.0f;
      s.reverse = { true, false, false, false, false, false, false, false }; add ("REVERSE", s); }
    { Spec s { "Reverse Roll" }; s.repeats = 8; s.division = 7; s.motion = 30.0f; s.decayDb = -6.0f; s.amount = 45.0f;
      s.choke = true; s.tightMs = 15.0f;
      s.reverse = { true, true, true, true, true, true, true, true }; add ("REVERSE", s); }

    { Spec s { "Locked Grid" }; s.decayDb = -12.0f; s.quantize = true; add ("ESSENTIALS", s); }
    { Spec s { "Locked Vocal Echo" }; s.repeats = 3; s.division = 2; s.decayDb = -10.0f; s.sourceMs = 200.0f;
      s.thresholdDb = -30.0f; s.retriggerMs = 250.0f; s.quantize = true; add ("VOCAL & CHOPS", s); }

    return all;
}

const std::vector<FactoryPreset>& presets()
{
    static const auto all = makeAll();

    return all;
}
}

const juce::StringArray& getPresetCategories()
{
    static const juce::StringArray categories { "ESSENTIALS", "ROLLS & FILLS", "PITCH", "ECHO & SPACE",
                                                "GROOVE", "VOCAL & CHOPS", "CHOKE & TIGHT", "REVERSE", "FX & DROPS" };
    return categories;
}

int getNumFactoryPresets() noexcept  { return (int) presets().size(); }

const FactoryPreset& getFactoryPreset (int index)
{
    return presets()[(size_t) juce::jlimit (0, getNumFactoryPresets() - 1, index)];
}
}

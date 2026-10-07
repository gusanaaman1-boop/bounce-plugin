#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

namespace bounce
{
/*
    The fixed V1 control contract (spec section 3). These IDs are permanent: a released ID is
    never renamed and never reused for a different meaning. 14 global + 32 point = 46.
*/
namespace id
{
    inline constexpr const char* enabled     = "enabled";
    inline constexpr const char* amount      = "amount";
    inline constexpr const char* wetOnly     = "wetOnly";
    inline constexpr const char* outputDb    = "outputDb";
    inline constexpr const char* repeats     = "repeats";
    inline constexpr const char* sync        = "sync";
    inline constexpr const char* division    = "division";
    inline constexpr const char* freeMs      = "freeMs";
    inline constexpr const char* motion      = "motion";
    inline constexpr const char* decayDb     = "decayDb";
    inline constexpr const char* pitchPathSt = "pitchPathSt";
    inline constexpr const char* sourceMs    = "sourceMs";
    inline constexpr const char* thresholdDb = "thresholdDb";
    inline constexpr const char* retriggerMs = "retriggerMs";

    // Options added in 1.1 (schema 2). Both default off, so the 1.0 sound is unchanged.
    inline constexpr const char* choke       = "choke";
    inline constexpr const char* tightMs     = "tightMs";

    // 1.3 (schema 3): grid lock and per-tap reverse. Default off.
    inline constexpr const char* quantize    = "quantize";

    // Point slots are 1-based in their IDs: tap1On ... tap8PitchSt.
    juce::String tapOn (int slot);
    juce::String tapTime (int slot);
    juce::String tapLevelDb (int slot);
    juce::String tapPitchSt (int slot);
    juce::String tapReverse (int slot);     // 1.3
}

inline constexpr int numSlots = 8;
// 14 from the V1 contract + choke, tightMs (1.1) + quantize (1.3)
inline constexpr int numGlobalParameters = 17;
// On / Time / LevelDb / PitchSt per slot (V1) + Reverse per slot (1.3) = 57 in total.
inline constexpr int numParameters = numGlobalParameters + 5 * numSlots;

// The parameter version hint passed to every ParameterID. Bumped only for new parameters.
inline constexpr int parameterVersion = 1;
inline constexpr int parameterVersionOptions = 2;   // choke, tightMs
inline constexpr int parameterVersionReverse = 3;   // quantize, tap{i}Reverse

// Division choice order is part of saved state and automation - never reorder.
inline constexpr int numDivisions = 8;
inline constexpr std::array<double, numDivisions> divisionBeats { 1.0, 0.75, 0.5, 1.0 / 3.0, 0.375, 0.25, 1.0 / 6.0, 0.125 };
inline constexpr int defaultDivision = 5;   // 1/16
juce::StringArray divisionNames();

inline constexpr float tapTimeMin = 0.005f;
inline constexpr float tapTimeMax = 1.0f;
inline constexpr float defaultTapTime (int slotIndex0) noexcept  { return (float) (slotIndex0 + 1) / 8.0f; }

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}

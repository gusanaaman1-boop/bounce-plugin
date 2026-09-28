#pragma once

#include <array>
#include <cstdint>

namespace bounce
{
/*
    The timing, gain and pitch model of one trigger (spec sections 3, 4 and 6).

    Pure and allocation-free, so the audio thread snapshots a schedule at each detected onset
    and the editor computes the very same schedule to draw it - the balls on screen are the
    output of this function, not a separate drawing model.
*/
struct PatternSettings
{
    int   repeats = 4;
    bool  sync = true;
    int   division = 5;          // index into divisionBeats
    float freeMs = 125.0f;
    float motion = 0.0f;         // -100 SLOW DOWN ... +100 ACCELERATE
    float decayDb = -18.0f;
    float pitchPathSt = 0.0f;
    float sourceMs = 100.0f;
    float tightMs = 0.0f;        // 0 = off; else each repeat is only the hit's first tightMs
    bool  choke = false;         // a new hit cuts the repeats of the previous one

    std::array<bool,  8> on      { true, true, true, true, true, true, true, true };
    std::array<float, 8> time    { 0.125f, 0.25f, 0.375f, 0.5f, 0.625f, 0.75f, 0.875f, 1.0f };
    std::array<float, 8> levelDb {};
    std::array<float, 8> pitchSt {};
};

struct TapPlan
{
    bool    active = false;      // slot is within REPEATS
    bool    on = false;          // active and not muted - the tap is rendered
    int64_t delaySamples = 0;    // actual onset of this repeat after the detected hit
    double  nominalSamples = 0;  // before the source-safety shift and minimum gap
    float   levelDb = 0.0f;      // effective level (trim + decay share)
    float   gain = 0.0f;         // linear, 0 when muted
    float   pitchSt = 0.0f;      // effective pitch, clamped to +/-12
    double  rate = 1.0;          // varispeed playback rate 2^(pitch/12)
    int64_t lengthSamples = 0;   // how long this repeat plays: source / rate
};

struct Schedule
{
    std::array<TapPlan, 8> taps {};
    int     repeats = 4;
    double  sampleRate = 48000.0;
    double  bpm = 120.0;
    double  intervalSamples = 0.0;    // B
    double  phraseSamples = 0.0;      // T = N * B
    double  warpPower = 1.0;          // p = 2^(-motion/100)
    int64_t sourceSamples = 0;        // L, the whole capture window including pre-roll
    int64_t preRollSamples = 0;
    int64_t tightSamples = 0;         // 0 = off
    bool    choke = false;
    int64_t safetyShiftSamples = 0;   // common shift applied so the capture is complete in time
    double  capScale = 1.0;           // < 1 when the 16 s limit compressed the pattern
    bool    gapPushed = false;        // the 12 ms minimum gap moved at least one tap
    int64_t firstDelaySamples = 0;
    int64_t lastDelaySamples = 0;
    int64_t endSamples = 0;           // when the last repeat finishes playing
};

inline constexpr double preRollMs = 8.0;
inline constexpr double safetyMarginMs = 5.0;
inline constexpr double minimumGapMs = 12.0;
inline constexpr double maxPhraseSeconds = 16.0;
inline constexpr double fallbackBpm = 120.0;
inline constexpr float  minTimeSeparation = 0.002f;   // stored master-grid order margin

/** Forces the eight stored master-grid times into range and strictly increasing order.
    Deterministic, and leaves an already-valid set untouched. Returns true if anything moved. */
bool repairTapTimes (std::array<float, 8>& times) noexcept;

/** Computes the audible schedule of one trigger. bpm <= 0 or non-finite uses fallbackBpm. */
Schedule computeSchedule (const PatternSettings&, double sampleRate, double bpm) noexcept;

/** Inverse of the timing warp for editing: the stored master-grid time that puts a tap at
    nominal time t (samples, before any safety shift). */
double masterTimeForNominal (double nominalSamples, double phraseSamples, double warpPower, int repeats) noexcept;

inline int64_t samplesFromMs (double ms, double sampleRate) noexcept  { return (int64_t) (ms * 0.001 * sampleRate + 0.5); }
}

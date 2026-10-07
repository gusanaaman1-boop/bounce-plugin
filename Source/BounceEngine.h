#pragma once

#include "OnsetDetector.h"
#include "Pattern.h"

#include <array>
#include <cmath>
#include <atomic>
#include <cstdint>
#include <vector>

namespace bounce
{
/*
    The wet engine (spec sections 4-6): a monotonic plug-in-local sample clock, an 8 ms input
    history, a fixed pool of 16 captured events, and the finite set of taps each event plays.

    One detected hit becomes one event. The event copies the 8 ms before the onset from the
    history, keeps copying the live input until the capture window is full, and plays that
    one excerpt once per scheduled tap - each with its own snapshotted delay, gain and
    varispeed rate. Nothing is fed back: the detector only ever sees the original input.

    Real-time contract: prepare() is the only allocating call. process() touches only
    preallocated memory, takes no locks and makes no system calls. All pool memory is sized
    for a 1000 ms capture plus pre-roll at the prepared rate (192 kHz included):
        per event   2 x (sr + 8 ms + 16) floats    (1.5 MB at 192 kHz, 0.38 MB at 48 kHz)
        pool        16 events                      (~24.7 MB at 192 kHz, ~6.2 MB at 48 kHz)
    CPU is bounded by 16 events x 8 taps; an unpitched tap is a copy, a pitched tap is a
    16-32 point windowed-sinc read per output sample (see Resampler).
*/
class BounceEngine
{
public:
    static constexpr int maxEvents = 16;
    static constexpr double cancelFadeMs = 5.0;

    void prepare (double sampleRate, int numChannels);
    void reset() noexcept;                              // silence everything at once

    void setDetector (float thresholdDb, float retriggerMs) noexcept  { detector.setParameters (thresholdDb, retriggerMs); }

    /** The settings and tempo a trigger in the next process() call will snapshot. The whole
        block shares one parameter snapshot, taken at its start. */
    void setPattern (const PatternSettings& s, double bpm) noexcept;

    /** QUANTIZE needs the host grid: the PPQ position at the first sample of the next
        process() call and the samples per beat. valid = false when the host gives no position. */
    void setGridReference (bool valid, double ppqAtBlockStart, double samplesPerBeat) noexcept
    {
        gridValid = valid && std::isfinite (ppqAtBlockStart) && samplesPerBeat > 1.0;
        gridPpq = ppqAtBlockStart;
        gridSamplesPerBeat = samplesPerBeat;
    }

    /** Detection on/off. Off also fades all sounding events out. */
    void setDetecting (bool shouldDetect) noexcept;

    /** Fades every sounding event out over 5 ms and drops unfinished captures - used on
        transport stop, seek and loop jumps. */
    void cancelAll() noexcept  { cancelFrom (now); }

    /** Re-arms the onset detector with empty envelopes. Called when the transport starts or
        jumps: a host that stopped calling process() right after a hit (Cubase suspends idle
        plug-ins) would otherwise leave it frozen mid-hit and deaf to the first hit on play. */
    void rearmDetector() noexcept  { detector.reset(); }

    /** input: numChannels pointers (read only, must stay valid for the whole call).
        wet: numChannels pointers, overwritten with the wet signal for this block. */
    void process (const float* const* input, float* const* wet, int numSamples) noexcept;

    // --- diagnostics, readable from any thread
    uint32_t triggerCount() const noexcept      { return triggers.load (std::memory_order_relaxed); }
    uint32_t droppedCount() const noexcept      { return dropped.load (std::memory_order_relaxed); }
    int      activeEventCount() const noexcept  { return active.load (std::memory_order_relaxed); }
    uint32_t cancelledCount() const noexcept    { return cancelled.load (std::memory_order_relaxed); }
    int64_t  clock() const noexcept             { return now; }
    int      channels() const noexcept          { return numCh; }
    double   rate() const noexcept              { return sr; }
    int64_t  capacitySamples() const noexcept   { return capacity; }

    /** For tests: the schedule the most recent trigger snapshotted. */
    const Schedule& lastSchedule() const noexcept  { return lastTriggerSchedule; }
    int64_t lastTriggerClock() const noexcept      { return lastOnset; }

private:
    struct Tap
    {
        bool    play = false;
        int64_t start = 0;       // clock of excerpt index 0
        int64_t length = 0;      // output samples this tap lasts
        double  rate = 1.0;
        float   gain = 0.0f;
        bool    reverse = false; // reads from the end of the excerpt towards its start
    };

    struct Event
    {
        bool    inUse = false;
        bool    capturing = false;
        int64_t onset = 0;
        int64_t length = 0;      // capture window L
        int64_t preRoll = 0;
        int64_t written = 0;
        int64_t end = 0;         // clock after which every tap has finished
        int64_t fadeEnd = -1;    // >= 0 while cancelling
        bool    tight = false;
        std::array<Tap, 8> taps {};
        float* data[2] { nullptr, nullptr };
    };

    void startEvent (int64_t onsetClock) noexcept;
    void cancelFrom (int64_t clock) noexcept;
    void finishCapture (Event&) noexcept;
    void renderEvent (Event&, float* const* wet, int numSamples) noexcept;

    double sr = 48000.0;
    int numCh = 2;
    int64_t capacity = 0;
    int64_t preRollMax = 0;
    int64_t cancelFadeSamples = 240;

    std::vector<float> pool;
    std::array<Event, maxEvents> events {};

    static constexpr int historySize = 4096;   // power of two, > 8 ms at 192 kHz
    std::vector<float> history;                // historySize x 2, interleaved by channel block
    int64_t now = 0;

    bool gridValid = false;
    double gridPpq = 0.0, gridSamplesPerBeat = 24000.0;

    OnsetDetector detector;
    bool detecting = true;

    PatternSettings pendingSettings;
    double pendingBpm = fallbackBpm;
    bool scheduleDirty = true;
    Schedule cachedSchedule;
    Schedule lastTriggerSchedule;
    int64_t lastOnset = -1;

    std::atomic<uint32_t> triggers { 0 }, dropped { 0 }, cancelled { 0 };
    std::atomic<int> active { 0 };
};
}

#pragma once

#include <cstdint>

namespace bounce
{
/*
    Stereo-safe transient detector (spec section 5), fed the ORIGINAL input only.

    Input is max(|L|, |R|) per sample, so opposite-polarity channels cannot cancel a hit.
      fast envelope   instant attack / 1 ms release on the rectified signal (a peak follower)
      slow envelope   30 ms one-pole smoothing of the FAST envelope
    A trigger fires on the sample where all of these hold:
      - armed
      - fast is rising
      - fast >= threshold (absolute floor, dBFS)
      - fast >= 2 x slow (at least 6 dB of fast-over-slow contrast)
      - at least retriggerMs since the previous trigger
    After a trigger the detector disarms. It re-arms once fast has fallen 6 dB below the
    threshold, or below the slow envelope (the burst's contrast has fully gone - this lets a
    loud continuous loop, which never falls below the floor, still trigger on every hit).

    Onset tolerance: an impulse or a hard-clipped edge triggers on its first sample at any
    sample rate; a smooth attack triggers on the first sample whose magnitude crosses the
    threshold, typically within 0-2 ms of the audible start. The capture includes 8 ms of pre-roll, so the whole attack is always
    kept, and each repeat reproduces the same point of the hit at exactly its tap time.
*/
class OnsetDetector
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void setParameters (float thresholdDb, float retriggerMs) noexcept;

    /** Feeds one sample of max(|L|,|R|); returns true on the sample a new onset is detected. */
    bool process (float peak, int64_t clock) noexcept;

    float fastEnvelope() const noexcept  { return fast; }
    float slowEnvelope() const noexcept  { return slow; }

private:
    double sr = 48000.0;
    float fastAttack = 0.0f, fastRelease = 0.0f, slowCoeff = 0.0f;
    float threshold = 0.063f, rearmLevel = 0.0316f;
    int64_t retriggerSamples = 4320;

    float fast = 0.0f, slow = 0.0f;
    bool armed = true;
    int64_t lastTrigger = INT64_MIN / 2;
};
}

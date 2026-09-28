#pragma once

#include <array>
#include <cstdint>

namespace bounce
{
/*
    Band-limited fractional reader for varispeed playback at rates 0.5 ... 2.0 (spec section 6).

    A Kaiser-windowed sinc (beta 8, 8 zero crossings each side) is tabulated once at 2048
    points per zero crossing. For a read at rate r the kernel's cutoff is c = min(1, 1/r) of
    the source Nyquist, i.e. the kernel is stretched by 1/c in source samples, so a pitch-up
    read low-passes the source BEFORE it is decimated and cannot alias. Pitch-down (r < 1)
    keeps the full band. Taps per output sample: 16 at r <= 1, up to 32 at r = 2.

    A read at an integer position with r == 1 is an exact copy (the unpitched fast path).
    Reads outside [0, length) see zeros, so every index is bounded. The table is a function-
    local static built on first use (the processor touches it in prepareToPlay); it is never
    built on the audio thread.

    Own implementation - no third-party DSP code.
*/
class Resampler
{
public:
    static constexpr int zeroCrossings = 8;
    static constexpr int tableResolution = 2048;
    static constexpr int tableSize = zeroCrossings * tableResolution + 2;

    /** Builds the shared table. Safe to call repeatedly; call from prepareToPlay. */
    static void initialise() noexcept;

    /** Reads a sample of `data` (length samples) at fractional position pos, for playback
        rate `rate`. Two channels share the position so their phase relation is kept. */
    static void read2 (const float* left, const float* right, int64_t length,
                       double pos, double rate, float& outL, float& outR) noexcept;

    static float read1 (const float* data, int64_t length, double pos, double rate) noexcept;

private:
    static const std::array<float, tableSize>& table() noexcept;
};
}

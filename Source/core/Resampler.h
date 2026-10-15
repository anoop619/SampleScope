#pragma once

#include <vector>

namespace ss
{

/** Offline, band-limited resampler (Kaiser-windowed sinc, normalised per output sample).

    Used for three things:
      - downsampling to the analysis rate (zeroCrossings = 8: fast)
      - "Re-Pitch" warping, i.e. sampler-style varispeed (zeroCrossings = 32: transparent)
      - matching preview audio to the host sample rate

    When the ratio implies downsampling the kernel's cutoff is lowered automatically, so
    pitching material up never aliases.
*/
struct Resampler
{
    /** @param inPerOut  input samples consumed per output sample (2.0 = octave up / half length) */
    static void process (const float* in, int inLength,
                         float* out, int outLength,
                         double inPerOut, int zeroCrossings = 32);

    static std::vector<float> resample (const float* in, int inLength, double inPerOut, int zeroCrossings = 32);
};

} // namespace ss

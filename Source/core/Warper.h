#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace ss
{

/** Warp algorithms, named after what producers know from Ableton.

    Pro          Signalsmith Stretch (phase-vocoder family, multi-resolution). Independent
                 time and pitch, best general quality. Used for melodic loops, pads, leads.
    Pro+Formant  Pro with formant preservation, so transposed vocals don't "chipmunk".
    Re-Pitch     Band-limited sinc resampling, like a hardware sampler: the cleanest possible
                 sound, but pitch and speed move together. Ideal for one-shots (808s, plucks).
    Beats        Pro stretch with the original transients re-imposed at their new positions,
                 so drum hits stay crisp at any tempo.
    Auto         Picks one of the above per sample type.
*/
enum class WarpMode : int { Auto = 0, Pro, ProFormant, RePitch, Beats };

const char* warpModeName (WarpMode);

struct WarpParams
{
    double timeRatio = 1.0;          // output length / input length
    int    exactOutputLength = -1;   // when > 0, overrides timeRatio (bar-exact loops)
    double semitones = 0.0;
    WarpMode mode = WarpMode::Pro;   // must be resolved (not Auto)
};

struct Warper
{
    /** Offline processing of a whole buffer. Returns the warped audio. */
    static juce::AudioBuffer<float> process (const juce::AudioBuffer<float>& input, double sampleRate, const WarpParams&);

private:
    static juce::AudioBuffer<float> pro (const juce::AudioBuffer<float>&, double sr, int outLength, double semitones, bool formants);
    static juce::AudioBuffer<float> repitch (const juce::AudioBuffer<float>&, int outLength);
    static juce::AudioBuffer<float> beats (const juce::AudioBuffer<float>&, double sr, int outLength, double semitones);
};

} // namespace ss

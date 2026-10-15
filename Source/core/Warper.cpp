#include "Warper.h"
#include "DSPUtils.h"
#include "Resampler.h"

#include <signalsmith-stretch/signalsmith-stretch.h>

namespace ss
{

const char* warpModeName (WarpMode m)
{
    switch (m)
    {
        case WarpMode::Auto:       return "Auto";
        case WarpMode::Pro:        return "Pro";
        case WarpMode::ProFormant: return "Pro + Formants";
        case WarpMode::RePitch:    return "Re-Pitch";
        case WarpMode::Beats:      return "Beats";
    }
    return "Pro";
}

juce::AudioBuffer<float> Warper::process (const juce::AudioBuffer<float>& in, double sr, const WarpParams& p)
{
    const int inLen = in.getNumSamples();
    if (inLen == 0)
        return in;

    int outLen = p.exactOutputLength > 0 ? p.exactOutputLength
                                         : std::max (1, (int) std::lround (inLen * p.timeRatio));
    const bool stretch = outLen != inLen;
    const bool shift = std::abs (p.semitones) > 1.0e-4;

    if (! stretch && ! shift)
        return in;

    switch (p.mode)
    {
        case WarpMode::RePitch:
        {
            // Pitch follows speed. With no tempo change, the length follows the pitch instead.
            if (! stretch)
                outLen = std::max (1, (int) std::lround (inLen / std::pow (2.0, p.semitones / 12.0)));
            return repitch (in, outLen);
        }
        case WarpMode::Beats:
            return beats (in, sr, outLen, p.semitones);
        case WarpMode::ProFormant:
            return pro (in, sr, outLen, p.semitones, true);
        case WarpMode::Auto:
        case WarpMode::Pro:
        default:
            return pro (in, sr, outLen, p.semitones, false);
    }
}

juce::AudioBuffer<float> Warper::pro (const juce::AudioBuffer<float>& in, double sr, int outLen, double semitones, bool formants)
{
    const int ch = in.getNumChannels();
    const int inLen = in.getNumSamples();

    signalsmith::stretch::SignalsmithStretch<float> st;
    st.presetDefault (ch, (float) sr);
    st.setTransposeSemitones ((float) semitones, (float) (8000.0 / sr));
    if (formants)
    {
        st.setFormantFactor (1.0f, true);      // keep the formants where they were
        st.setFormantBase (0.0f);              // auto-detect the fundamental
    }

    // Very short sounds: pad with silence so the stretcher has a full analysis window,
    // then trim back.
    const double rate = (double) inLen / outLen;
    const int minIn = st.outputSeekLength ((float) rate) + st.blockSamples();
    const int padIn = std::max (inLen, minIn);
    const int padOut = (int) std::lround (padIn / rate);

    juce::AudioBuffer<float> src (ch, padIn);
    src.clear();
    for (int c = 0; c < ch; ++c) src.copyFrom (c, 0, in, c, 0, inLen);

    juce::AudioBuffer<float> dst (ch, padOut);
    dst.clear();

    st.exact (src.getArrayOfReadPointers(), padIn, dst.getArrayOfWritePointers(), padOut);

    juce::AudioBuffer<float> out (ch, outLen);
    for (int c = 0; c < ch; ++c) out.copyFrom (c, 0, dst, c, 0, std::min (outLen, padOut));
    if (padOut < outLen)
        for (int c = 0; c < ch; ++c) out.clear (c, padOut, outLen - padOut);
    return out;
}

juce::AudioBuffer<float> Warper::repitch (const juce::AudioBuffer<float>& in, int outLen)
{
    const int ch = in.getNumChannels();
    const double inPerOut = (double) in.getNumSamples() / outLen;
    juce::AudioBuffer<float> out (ch, outLen);
    for (int c = 0; c < ch; ++c)
        Resampler::process (in.getReadPointer (c), in.getNumSamples(), out.getWritePointer (c), outLen, inPerOut, 32);
    return out;
}

juce::AudioBuffer<float> Warper::beats (const juce::AudioBuffer<float>& in, double sr, int outLen, double semitones)
{
    // Transposing drums smears the transients we are trying to keep; Pro handles that case.
    if (std::abs (semitones) > 1.0e-4)
        return pro (in, sr, outLen, semitones, false);

    auto out = pro (in, sr, outLen, 0.0, false);

    const int ch = in.getNumChannels();
    const int inLen = in.getNumSamples();
    std::vector<float> mono ((size_t) inLen, 0.0f);
    for (int c = 0; c < ch; ++c)
    {
        const float* d = in.getReadPointer (c);
        for (int i = 0; i < inLen; ++i) mono[(size_t) i] += d[i] / (float) ch;
    }

    const auto onsets = dsp::detectOnsets (mono.data(), inLen, sr, 1.2f);
    const double scale = (double) outLen / inLen;
    const int pre = (int) (0.002 * sr), hold = (int) (0.022 * sr), fade = (int) (0.012 * sr);
    const int search = (int) (0.012 * sr);

    // Refine each onset to the actual rise in the waveform.
    std::vector<int> hits;
    for (int o : onsets.onsets)
    {
        const int a = std::max (0, o - search), b = std::min (inLen, o + search);
        float mx = 0.0f;
        for (int i = a; i < b; ++i) mx = std::max (mx, std::abs (mono[(size_t) i]));
        int at = a;
        while (at < b && std::abs (mono[(size_t) at]) < 0.5f * mx) ++at;
        if (hits.empty() || at - hits.back() > hold)
            hits.push_back (at);
    }

    for (size_t h = 0; h < hits.size(); ++h)
    {
        const int src = hits[h];
        const int dst = (int) std::lround (src * scale);
        const int nextDst = h + 1 < hits.size() ? (int) std::lround (hits[h + 1] * scale) : outLen;
        const int span = std::min (hold + fade, nextDst - dst - pre);
        if (span <= pre)
            continue;
        const int holdHere = std::min (hold, span - 1);
        const int fadeHere = std::max (1, span - holdHere);

        for (int j = -pre; j < span; ++j)
        {
            const int si = src + j, di = dst + j;
            if (si < 0 || si >= inLen || di < 0 || di >= outLen) continue;
            float w;
            if (j < 0)              w = (float) (j + pre) / (float) pre;
            else if (j < holdHere)  w = 1.0f;
            else                    w = 1.0f - (float) (j - holdHere) / (float) fadeHere;
            w = juce::jlimit (0.0f, 1.0f, w);
            for (int c = 0; c < ch; ++c)
            {
                auto* o = out.getWritePointer (c);
                o[di] = (1.0f - w) * o[di] + w * in.getSample (c, si);
            }
        }
    }
    return out;
}

} // namespace ss

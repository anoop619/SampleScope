#pragma once

// Synthetic, labelled test material. Every signal is generated deterministically so the
// tests are reproducible on any machine (and in CI) without shipping audio files.

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <functional>
#include <random>

namespace sig
{
constexpr double SR = 44100.0;
constexpr double PI = 3.14159265358979323846;

using Buffer = juce::AudioBuffer<float>;

inline Buffer make (double seconds, int channels = 1)
{
    Buffer b (channels, (int) std::lround (seconds * SR));
    b.clear();
    return b;
}

inline float midiHz (double m) { return (float) (440.0 * std::pow (2.0, (m - 69.0) / 12.0)); }

struct Noise
{
    std::mt19937 rng { 1234 };
    std::uniform_real_distribution<float> d { -1.0f, 1.0f };
    float operator()() { return d (rng); }
};

/** Simple RBJ biquad. */
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    static Biquad make (int type, double f, double q)   // 0 LP, 1 HP, 2 BP
    {
        Biquad bq;
        const double w = 2 * PI * f / SR, c = std::cos (w), s = std::sin (w), al = s / (2 * q);
        double B0, B1, B2, A0 = 1 + al, A1 = -2 * c, A2 = 1 - al;
        if (type == 0)      { B0 = (1 - c) / 2; B1 = 1 - c;    B2 = (1 - c) / 2; }
        else if (type == 1) { B0 = (1 + c) / 2; B1 = -(1 + c); B2 = (1 + c) / 2; }
        else                { B0 = al;          B1 = 0;        B2 = -al; }
        bq.b0 = B0 / A0; bq.b1 = B1 / A0; bq.b2 = B2 / A0; bq.a1 = A1 / A0; bq.a2 = A2 / A0;
        return bq;
    }
    float operator() (float x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
};

inline void normalise (Buffer& b, float peakDb = -1.0f)
{
    const float mag = b.getMagnitude (0, b.getNumSamples());
    if (mag > 0) b.applyGain (juce::Decibels::decibelsToGain (peakDb) / mag);
}

inline float saw (double phase) { return (float) (2.0 * (phase - std::floor (phase + 0.5))); }

//==============================================================================
inline Buffer kick (float len = 0.6f)
{
    auto b = make (len); Noise n; double ph = 0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double t = i / SR;
        const double f = 50 + 110 * std::exp (-t / 0.03);
        ph += f / SR;
        float v = (float) (std::sin (2 * PI * ph) * std::exp (-t / 0.25));
        if (t < 0.002) v += 0.25f * n();
        b.setSample (0, i, v);
    }
    normalise (b); return b;
}

inline Buffer eightOhEight (double midi = 31)  // G1
{
    auto b = make (1.8); double ph = 0; const double f0 = midiHz (midi);
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double t = i / SR;
        ph += (f0 + 40 * std::exp (-t / 0.02)) / SR;
        b.setSample (0, i, (float) std::tanh (1.5 * std::sin (2 * PI * ph)) * (float) std::exp (-t / 0.8));
    }
    normalise (b); return b;
}

inline Buffer snare()
{
    auto b = make (0.4); Noise n; auto bp = Biquad::make (2, 3000, 0.7); double ph = 0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double t = i / SR;
        ph += 190 / SR;
        b.setSample (0, i, (float) (0.5 * std::sin (2 * PI * ph) * std::exp (-t / 0.05)) + bp (n()) * (float) std::exp (-t / 0.12) * 1.5f);
    }
    normalise (b); return b;
}

inline Buffer clap()
{
    auto b = make (0.5); Noise n; auto bp = Biquad::make (2, 1200, 1.0);
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double t = i / SR;
        double env = 0;
        for (double s : { 0.0, 0.011, 0.022 })
            if (t >= s && t < s + 0.009) env = std::max (env, std::exp (-(t - s) / 0.003));
        if (t >= 0.03) env = std::max (env, 0.7 * std::exp (-(t - 0.03) / 0.1));
        b.setSample (0, i, bp (n()) * (float) env);
    }
    normalise (b); return b;
}

inline Buffer hat (float len, float decay, double hp = 7000)
{
    auto b = make (len); Noise n; auto h1 = Biquad::make (1, hp, 0.7); auto h2 = Biquad::make (1, hp, 0.7);
    for (int i = 0; i < b.getNumSamples(); ++i)
        b.setSample (0, i, h2 (h1 (n())) * (float) std::exp (-(i / SR) / decay));
    normalise (b); return b;
}

inline Buffer bassNote (double midi = 33, float len = 0.9f)
{
    auto b = make (len); auto lp = Biquad::make (0, 600, 0.8); double ph = 0; const double f = midiHz (midi);
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double t = i / SR; ph += f / SR;
        const double env = std::min (1.0, t / 0.005) * std::exp (-t / 0.6);
        b.setSample (0, i, lp (saw (ph)) * (float) env);
    }
    normalise (b); return b;
}

inline Buffer pluck (double midi = 60)
{
    auto b = make (1.0); Noise n;
    const int period = (int) std::lround (SR / midiHz (midi));
    std::vector<float> line ((size_t) period);
    for (auto& v : line) v = n();
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const int k = i % period;
        const float out = line[(size_t) k];
        line[(size_t) k] = 0.996f * 0.5f * (line[(size_t) k] + line[(size_t) ((k + 1) % period)]);
        b.setSample (0, i, out);
    }
    normalise (b); return b;
}

inline void addChord (Buffer& b, std::initializer_list<double> notes, double start, double len,
                      double attack, double decay, float gain, double cutoff = 2500)
{
    auto lp = Biquad::make (0, cutoff, 0.7);
    std::vector<double> phases (notes.size() * 3, 0.0);
    const int s0 = (int) (start * SR), s1 = std::min (b.getNumSamples(), (int) ((start + len) * SR));
    for (int i = s0; i < s1; ++i)
    {
        const double t = (i - s0) / SR;
        float v = 0; int k = 0;
        for (double m : notes)
            for (double det : { -0.08, 0.0, 0.08 })
            {
                phases[(size_t) k] += midiHz (m + det) / SR;
                v += saw (phases[(size_t) k]) / 3.0f;
                ++k;
            }
        const double env = std::min (1.0, t / attack) * (decay > 0 ? std::exp (-t / decay) : 1.0)
                         * std::min (1.0, (len - t) / 0.02);
        for (int c = 0; c < b.getNumChannels(); ++c)
            b.addSample (c, i, gain * lp (v) * (float) env);
    }
}

inline Buffer pad()          // A minor, slow attack
{
    auto b = make (4.0, 2);
    addChord (b, { 57, 60, 64 }, 0.0, 4.0, 0.6, 0.0, 0.3f, 2000);
    normalise (b); return b;
}

inline Buffer chordStab()    // C major, fast attack
{
    auto b = make (0.7);
    addChord (b, { 60, 64, 67 }, 0.0, 0.7, 0.003, 0.25, 0.3f, 3000);
    normalise (b); return b;
}

/** Am - F - C - G, 8th-note arpeggios plus bass, 4 bars. */
inline Buffer melodicLoop (double bpm = 124)
{
    const double beat = 60.0 / bpm;
    auto b = make (16 * beat, 2);
    const std::vector<std::vector<double>> chords { { 57, 60, 64 }, { 53, 57, 60 }, { 48, 52, 55 }, { 55, 59, 62 } };
    const double bassNotes[] = { 45, 41, 48, 43 };
    for (int bar = 0; bar < 4; ++bar)
    {
        const auto& ch = chords[(size_t) bar];
        for (int e = 0; e < 8; ++e)
        {
            const double note = ch[(size_t) (e % 3)] + (e >= 4 ? 12 : 0);
            addChord (b, { note }, (bar * 4 + e * 0.5) * beat, 0.5 * beat, 0.003, 0.18, 0.25f, 3500);
        }
        addChord (b, { bassNotes[bar] - 12 }, bar * 4 * beat, 4 * beat, 0.005, 1.5, 0.35f, 400);
    }
    normalise (b); return b;
}

inline void place (Buffer& dst, const Buffer& src, double atSeconds, float gain = 1.0f)
{
    const int s0 = (int) std::lround (atSeconds * SR);
    for (int c = 0; c < dst.getNumChannels(); ++c)
        for (int i = 0; i < src.getNumSamples() && s0 + i < dst.getNumSamples(); ++i)
            dst.addSample (c, s0 + i, gain * src.getSample (std::min (c, src.getNumChannels() - 1), i));
}

inline Buffer drumLoop (double bpm = 95)
{
    const double beat = 60.0 / bpm;
    auto b = make (8 * beat, 2);
    auto k = kick (0.5f), s = snare(), h = hat (0.1f, 0.03f);
    for (int bar = 0; bar < 2; ++bar)
    {
        const double o = bar * 4 * beat;
        place (b, k, o); place (b, k, o + 1.5 * beat, 0.8f); place (b, k, o + 2 * beat);
        place (b, s, o + beat, 0.8f); place (b, s, o + 3 * beat, 0.8f);
        for (int e = 0; e < 8; ++e) place (b, h, o + e * 0.5 * beat, 0.35f);
    }
    normalise (b); return b;
}

inline Buffer hatLoop (double bpm = 128)
{
    const double beat = 60.0 / bpm;
    auto b = make (4 * beat, 2);
    auto h = hat (0.08f, 0.025f);
    for (int s = 0; s < 16; ++s) place (b, h, s * 0.25 * beat, s % 2 == 0 ? 0.9f : 0.5f);
    normalise (b); return b;
}

inline Buffer bassLoop (double bpm = 140)    // F minor
{
    const double beat = 60.0 / bpm;
    auto b = make (8 * beat, 2);
    const double notes[] = { 29, 29, 32, 29, 36, 29, 34, 32, 29, 29, 32, 29, 36, 39, 36, 32 };
    for (int e = 0; e < 16; ++e)
        addChord (b, { notes[e] + 12 }, e * 0.5 * beat, 0.45 * beat, 0.004, 0.3, 0.4f, 500);
    normalise (b); return b;
}

inline Buffer riser()
{
    auto b = make (4.0, 2); Noise n;
    double z1 = 0, z2 = 0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double t = i / SR;
        const double f = 300 * std::pow (8000.0 / 300.0, t / 4.0);
        auto bp = Biquad::make (2, f, 2.0); bp.z1 = z1; bp.z2 = z2;
        const float v = bp (n()) * (float) (0.1 + 0.9 * t / 4.0);
        z1 = bp.z1; z2 = bp.z2;
        b.setSample (0, i, v); b.setSample (1, i, v);
    }
    normalise (b); return b;
}

inline Buffer vocalVowel()
{
    auto b = make (1.6); double ph = 0;
    std::vector<Biquad> formants { Biquad::make (2, 800, 6), Biquad::make (2, 1150, 8), Biquad::make (2, 2900, 10) };
    const float gains[] = { 1.0f, 0.6f, 0.25f };
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double t = i / SR;
        const double cents = 45 * std::sin (2 * PI * 5.5 * t) + 120 * std::min (1.0, t / 0.4);
        ph += 196.0 * std::pow (2.0, cents / 1200.0) / SR;
        const float src = saw (ph);
        float v = 0;
        for (size_t f = 0; f < formants.size(); ++f) v += gains[f] * formants[f] (src);
        const double env = std::min (1.0, t / 0.06) * std::min (1.0, (1.6 - t) / 0.15);
        b.setSample (0, i, v * (float) env);
    }
    normalise (b); return b;
}

inline Buffer texture()
{
    auto b = make (6.0, 2); Noise n; auto lp = Biquad::make (0, 1500, 0.5); auto lp2 = Biquad::make (0, 1200, 0.5);
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double t = i / SR;
        const double env = std::min (1.0, t / 1.5) * (0.6 + 0.4 * std::sin (2 * PI * 0.3 * t)) * std::min (1.0, (6.0 - t) / 0.5);
        b.setSample (0, i, lp (n()) * (float) env);
        b.setSample (1, i, lp2 (n()) * (float) env);
    }
    normalise (b); return b;
}

} // namespace sig

#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>

namespace ss::dsp
{

constexpr double pi = 3.14159265358979323846;

//==============================================================================
/** Windowed magnitude spectrum of a frame (Hann window, zero-padded if short). */
class Spectrum
{
public:
    explicit Spectrum (int fftOrder);

    int size() const noexcept       { return n; }
    int numBins() const noexcept    { return n / 2 + 1; }

    /** Returns numBins() magnitudes. `available` may be < size(); the rest is zero-padded. */
    const float* compute (const float* frame, int available);

private:
    int n;
    juce::dsp::FFT fft;
    std::vector<float> window, work;
};

//==============================================================================
struct OnsetResult
{
    std::vector<float> envelope;     // spectral-flux novelty curve, normalised 0..1
    double frameRate = 0;            // envelope frames per second
    int hop = 0;                     // samples per envelope frame
    std::vector<int> onsets;         // onset positions in samples
};

/** Log-compressed spectral-flux onset detector with adaptive peak picking. */
OnsetResult detectOnsets (const float* x, int length, double sampleRate, float sensitivity = 1.0f);

/** Tempo from an onset envelope (autocorrelation + comb over 4 metrical levels). */
float estimateTempo (const OnsetResult&, float minBpm = 60.0f, float maxBpm = 200.0f, float* strength = nullptr);

//==============================================================================
struct Peak { float freq, mag; };

void findPeaks (const float* mags, int numBins, double binHz, float minHz, float maxHz,
                float relThreshold, int maxPeaks, std::vector<Peak>& out);

/** Weighted circular mean of each peak's deviation from 12-TET, in cents (-50..50). */
float estimateTuningCents (const std::vector<Peak>& peaks);

void addPeaksToChroma (const std::vector<Peak>& peaks, float tuningCents,
                       std::array<float, 12>& chroma, std::array<float, 12>* bassChroma = nullptr);

/** Key from a chroma vector (Temperley + Krumhansl profiles). Returns -1 when flat/silent. */
int estimateKey (const std::array<float, 12>& chroma, float& confidence);

/** Major/minor triad that best explains a chroma vector (for chord one-shots). */
int estimateTriad (const std::array<float, 12>& chroma, float& confidence);

/** 0..1 how concentrated the chroma is (1 = single pitch class). */
float chromaPeakiness (const std::array<float, 12>& chroma);

//==============================================================================
/** YIN fundamental-frequency estimator. The difference function is computed through an
    FFT autocorrelation, so a frame costs two small FFTs instead of W x tauMax multiplies. */
class Yin
{
public:
    Yin (int windowSize, int maxLag);

    /** Needs windowSize + maxLag samples at x. Returns the period in samples (fractional) or 0,
        and the aperiodicity (0 = perfectly periodic). */
    float period (const float* x, int tauMin, float threshold, float& aperiodicity);

private:
    int W, tauMax, n;
    juce::dsp::FFT fft;
    std::vector<float> a, b, d, energy;
};

inline float hzToMidi (float hz)   { return 69.0f + 12.0f * std::log2 (hz / 440.0f); }
inline float midiToHz (float midi) { return 440.0f * std::pow (2.0f, (midi - 69.0f) / 12.0f); }

} // namespace ss::dsp

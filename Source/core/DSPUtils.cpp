#include "DSPUtils.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace ss::dsp
{

//==============================================================================
Spectrum::Spectrum (int fftOrder)
    : n (1 << fftOrder), fft (fftOrder), window ((size_t) n), work ((size_t) (2 * n))
{
    for (int i = 0; i < n; ++i)
        window[(size_t) i] = 0.5f - 0.5f * (float) std::cos (2.0 * pi * i / n);
}

const float* Spectrum::compute (const float* frame, int available)
{
    available = std::clamp (available, 0, n);
    for (int i = 0; i < available; ++i)
        work[(size_t) i] = frame[i] * window[(size_t) i];
    std::fill (work.begin() + available, work.end(), 0.0f);
    fft.performFrequencyOnlyForwardTransform (work.data(), true);
    return work.data();
}

//==============================================================================
OnsetResult detectOnsets (const float* x, int length, double sampleRate, float sensitivity)
{
    OnsetResult r;
    if (length <= 0 || sampleRate <= 0)
        return r;

    const int hop = std::max (32, (int) std::lround (sampleRate * 0.0058));
    int order = 5;
    while ((1 << order) < hop * 8) ++order;

    Spectrum spec (order);
    const int N = spec.size();
    const int bins = spec.numBins();
    const int kMin = std::max (1, (int) (30.0 * N / sampleRate));
    const float scale = 100.0f / (N * 0.25f);

    const int numFrames = 1 + (length - 1) / hop;
    r.hop = hop;
    r.frameRate = sampleRate / hop;
    r.envelope.assign ((size_t) numFrames, 0.0f);

    std::vector<float> frame ((size_t) N), prev ((size_t) bins, 0.0f);

    for (int t = 0; t < numFrames; ++t)
    {
        const int start = t * hop - N / 2;
        for (int i = 0; i < N; ++i)
        {
            const int idx = start + i;
            frame[(size_t) i] = (idx >= 0 && idx < length) ? x[idx] : 0.0f;
        }

        const float* mags = spec.compute (frame.data(), N);
        float flux = 0.0f;
        for (int k = kMin; k < bins; ++k)
        {
            const float y = std::log1p (scale * mags[k]);
            flux += std::max (0.0f, y - prev[(size_t) k]);
            prev[(size_t) k] = y;
        }
        r.envelope[(size_t) t] = flux;
    }

    const float maxV = *std::max_element (r.envelope.begin(), r.envelope.end());
    if (maxV <= 1.0e-9f)
        return r;
    for (auto& v : r.envelope) v /= maxV;

    // Adaptive peak picking (Dixon / Boeck style).
    const double fr = r.frameRate;
    const int wMax  = std::max (1, (int) std::lround (0.03 * fr));
    const int wPre  = std::max (1, (int) std::lround (0.10 * fr));
    const int wPost = std::max (1, (int) std::lround (0.07 * fr));
    const int minGap = std::max (1, (int) std::lround (0.03 * fr));
    const float delta = 0.06f / std::max (0.1f, sensitivity);

    int last = -minGap;
    for (int t = 0; t < numFrames; ++t)
    {
        const float v = r.envelope[(size_t) t];
        if (v < delta)
            continue;

        bool isMax = true;
        for (int j = std::max (0, t - wMax); j <= std::min (numFrames - 1, t + wMax) && isMax; ++j)
            if (r.envelope[(size_t) j] > v) isMax = false;
        if (! isMax)
            continue;

        const int a = std::max (0, t - wPre), b = std::min (numFrames - 1, t + wPost);
        float mean = 0.0f;
        for (int j = a; j <= b; ++j) mean += r.envelope[(size_t) j];
        mean /= (float) (b - a + 1);

        if (v >= mean + delta && t - last >= minGap)
        {
            r.onsets.push_back (std::max (0, t * hop));
            last = t;
        }
    }

    return r;
}

float estimateTempo (const OnsetResult& onsets, float minBpm, float maxBpm, float* strength)
{
    if (strength) *strength = 0.0f;
    const auto& env = onsets.envelope;
    const int n = (int) env.size();
    const double fr = onsets.frameRate;
    if (n < 16 || fr <= 0)
        return 0.0f;

    // Remove the local mean so sustained energy does not dominate the ACF.
    std::vector<float> e ((size_t) n);
    const int w = std::max (1, (int) (0.25 * fr));
    {
        double run = 0.0; int cnt = 0;
        int lo = 0, hi = -1;
        for (int i = 0; i < n; ++i)
        {
            while (hi < std::min (n - 1, i + w)) { ++hi; run += env[(size_t) hi]; ++cnt; }
            while (lo < i - w)                   { run -= env[(size_t) lo]; ++lo; --cnt; }
            e[(size_t) i] = std::max (0.0f, env[(size_t) i] - (float) (run / std::max (1, cnt)));
        }
    }

    const int maxLag = std::min (n - 1, (int) std::ceil (60.0 / minBpm * fr * 4.0) + 2);
    std::vector<double> acf ((size_t) maxLag + 1, 0.0);
    for (int lag = 0; lag <= maxLag; ++lag)
    {
        double s = 0.0;
        for (int i = 0; i + lag < n; ++i)
            s += (double) e[(size_t) i] * e[(size_t) (i + lag)];
        acf[(size_t) lag] = s / (n - lag);
    }
    if (acf[0] <= 0.0)
        return 0.0f;

    auto acfAt = [&] (double lag) -> double
    {
        int i = (int) lag;
        if (i < 0 || i + 1 > maxLag) return -1.0;
        double f = lag - i;
        return acf[(size_t) i] * (1.0 - f) + acf[(size_t) i + 1] * f;
    };

    const int usableLag = std::min (maxLag, n / 2);
    double bestScore = -1.0, bestBpm = 0.0;

    for (double bpm = minBpm; bpm <= maxBpm; bpm += 0.05)
    {
        const double L = 60.0 / bpm * fr;
        static const double weights[] = { 1.0, 0.6, 0.4, 0.45 };
        double s = 0.0, wsum = 0.0;
        for (int m = 1; m <= 4; ++m)
        {
            if (m * L > usableLag) break;
            s += weights[m - 1] * acfAt (m * L);
            wsum += weights[m - 1];
        }
        if (wsum <= 0.0) continue;
        s /= wsum;
        s += 0.25 * std::max (0.0, acfAt (L * 0.5));        // off-beats support the beat level

        const double oct = std::log2 (bpm / 118.0) / 1.0;
        const double prior = std::exp (-0.5 * oct * oct);
        const double score = s * (0.55 + 0.45 * prior);

        if (score > bestScore) { bestScore = score; bestBpm = bpm; }
    }

    if (strength) *strength = (float) std::clamp (bestScore / acf[0], 0.0, 1.0);
    return (float) bestBpm;
}

//==============================================================================
void findPeaks (const float* mags, int numBins, double binHz, float minHz, float maxHz,
                float relThreshold, int maxPeaks, std::vector<Peak>& out)
{
    out.clear();
    const int k0 = std::max (1, (int) std::ceil (minHz / binHz));
    const int k1 = std::min (numBins - 2, (int) std::floor (maxHz / binHz));
    if (k1 <= k0)
        return;

    float maxMag = 0.0f;
    for (int k = k0; k <= k1; ++k) maxMag = std::max (maxMag, mags[k]);
    if (maxMag <= 1.0e-9f)
        return;
    const float thr = maxMag * relThreshold;

    for (int k = k0; k <= k1; ++k)
    {
        const float m = mags[k];
        if (m <= thr || m <= mags[k - 1] || m < mags[k + 1])
            continue;

        const float a = std::log (mags[k - 1] + 1.0e-12f), b = std::log (m + 1.0e-12f), c = std::log (mags[k + 1] + 1.0e-12f);
        const float denom = a - 2.0f * b + c;
        const float p = std::abs (denom) > 1.0e-9f ? std::clamp (0.5f * (a - c) / denom, -0.5f, 0.5f) : 0.0f;
        out.push_back ({ (float) ((k + p) * binHz), std::exp (b - 0.25f * (a - c) * p) });
    }

    if ((int) out.size() > maxPeaks)
    {
        std::partial_sort (out.begin(), out.begin() + maxPeaks, out.end(),
                           [] (const Peak& x, const Peak& y) { return x.mag > y.mag; });
        out.resize ((size_t) maxPeaks);
    }
}

float estimateTuningCents (const std::vector<Peak>& peaks)
{
    double sx = 0.0, sy = 0.0;
    for (const auto& p : peaks)
    {
        if (p.freq <= 0.0f) continue;
        const double midi = hzToMidi (p.freq);
        const double dev = midi - std::round (midi);       // -0.5..0.5 semitones
        const double ang = dev * 2.0 * pi;
        sx += p.mag * std::cos (ang);
        sy += p.mag * std::sin (ang);
    }
    if (std::abs (sx) + std::abs (sy) < 1.0e-12)
        return 0.0f;
    return (float) (std::atan2 (sy, sx) / (2.0 * pi) * 100.0);
}

void addPeaksToChroma (const std::vector<Peak>& peaks, float tuningCents,
                       std::array<float, 12>& chroma, std::array<float, 12>* bassChroma)
{
    for (const auto& p : peaks)
    {
        if (p.freq <= 0.0f) continue;
        const float midi = hzToMidi (p.freq) - tuningCents / 100.0f;
        const int pc = (((int) std::lround (midi)) % 12 + 12) % 12;
        const float w = p.mag * (p.freq > 1200.0f ? 1200.0f / p.freq : 1.0f);
        chroma[(size_t) pc] += w;
        if (bassChroma != nullptr && p.freq < 220.0f)
            (*bassChroma)[(size_t) pc] += p.mag;
    }
}

namespace
{
    constexpr float kMajK[12] = { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
    constexpr float kMinK[12] = { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };
    constexpr float kMajT[12] = { 5.0f, 2.0f, 3.5f, 2.0f, 4.5f, 4.0f, 2.0f, 4.5f, 2.0f, 3.5f, 1.5f, 4.0f };
    constexpr float kMinT[12] = { 5.0f, 2.0f, 3.5f, 4.5f, 2.0f, 4.0f, 2.0f, 4.5f, 3.5f, 2.0f, 1.5f, 4.0f };

    float pearson (const std::array<float, 12>& x, const float* profile, int rotation)
    {
        float mx = 0, my = 0;
        for (int i = 0; i < 12; ++i) { mx += x[(size_t) i]; my += profile[i]; }
        mx /= 12; my /= 12;
        float sxy = 0, sxx = 0, syy = 0;
        for (int i = 0; i < 12; ++i)
        {
            const float a = x[(size_t) ((i + rotation) % 12)] - mx;
            const float b = profile[i] - my;
            sxy += a * b; sxx += a * a; syy += b * b;
        }
        return (sxx > 0 && syy > 0) ? sxy / std::sqrt (sxx * syy) : 0.0f;
    }
}

int estimateKey (const std::array<float, 12>& chroma, float& confidence)
{
    confidence = 0.0f;
    const float total = std::accumulate (chroma.begin(), chroma.end(), 0.0f);
    if (total <= 1.0e-9f)
        return -1;

    std::array<float, 24> scores {};
    for (int r = 0; r < 12; ++r)
    {
        scores[(size_t) r]      = 0.5f * (pearson (chroma, kMajK, r) + pearson (chroma, kMajT, r));
        scores[(size_t) r + 12] = 0.5f * (pearson (chroma, kMinK, r) + pearson (chroma, kMinT, r)) + 0.01f;
    }

    int best = 0;
    for (int i = 1; i < 24; ++i)
        if (scores[(size_t) i] > scores[(size_t) best]) best = i;

    float second = -1.0f;
    for (int i = 0; i < 24; ++i)
        if (i != best) second = std::max (second, scores[(size_t) i]);

    const float b = scores[(size_t) best];
    confidence = std::clamp ((b - 0.3f) / 0.55f, 0.0f, 1.0f) * std::clamp (0.35f + 6.0f * (b - second), 0.0f, 1.0f);
    return best;
}

int estimateTriad (const std::array<float, 12>& chroma, float& confidence)
{
    confidence = 0.0f;
    static const float major[12] = { 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0 };
    static const float minor[12] = { 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0 };

    int best = -1; float bestScore = -2.0f;
    for (int r = 0; r < 12; ++r)
    {
        const float a = pearson (chroma, major, r);
        const float b = pearson (chroma, minor, r);
        if (a > bestScore) { bestScore = a; best = r; }
        if (b > bestScore) { bestScore = b; best = r + 12; }
    }
    confidence = std::clamp (bestScore, 0.0f, 1.0f);
    return best;
}

float chromaPeakiness (const std::array<float, 12>& chroma)
{
    const float total = std::accumulate (chroma.begin(), chroma.end(), 0.0f);
    if (total <= 1.0e-9f) return 0.0f;
    const float mx = *std::max_element (chroma.begin(), chroma.end());
    return std::clamp ((mx / total - 1.0f / 12.0f) / (1.0f - 1.0f / 12.0f), 0.0f, 1.0f);
}

//==============================================================================
Yin::Yin (int windowSize, int maxLag) : W (windowSize), tauMax (maxLag), n (0), fft ([&]
{
    int order = 4;
    while ((1 << order) < windowSize + maxLag + 1) ++order;
    return order;
}())
{
    n = fft.getSize();
    a.resize ((size_t) (2 * n));
    b.resize ((size_t) (2 * n));
    d.resize ((size_t) tauMax + 1);
    energy.resize ((size_t) (W + tauMax + 1));
}

float Yin::period (const float* x, int tauMin, float threshold, float& aperiodicity)
{
    aperiodicity = 1.0f;
    if (tauMax <= tauMin + 2)
        return 0.0f;

    // r(tau) = sum_{j<W} x[j] x[j+tau]  via  IFFT( conj(FFT(x[0..W))) * FFT(x[0..W+tauMax)) )
    std::fill (a.begin(), a.end(), 0.0f);
    std::fill (b.begin(), b.end(), 0.0f);
    std::copy (x, x + W, a.begin());
    std::copy (x, x + W + tauMax, b.begin());
    fft.performRealOnlyForwardTransform (a.data(), true);
    fft.performRealOnlyForwardTransform (b.data(), true);
    for (int k = 0; k <= n / 2; ++k)
    {
        const float ar = a[(size_t) (2 * k)], ai = a[(size_t) (2 * k + 1)];
        const float br = b[(size_t) (2 * k)], bi = b[(size_t) (2 * k + 1)];
        b[(size_t) (2 * k)]     = ar * br + ai * bi;     // conj(A) * B
        b[(size_t) (2 * k + 1)] = ar * bi - ai * br;
    }
    fft.performRealOnlyInverseTransform (b.data());     // b[tau] = r(tau)

    // Running energies: e(tau) = sum_{j<W} x[j+tau]^2
    energy[0] = 0.0f;
    for (int j = 0; j < W + tauMax; ++j) energy[(size_t) j + 1] = energy[(size_t) j] + x[j] * x[j];
    const float e0 = energy[(size_t) W];

    d[0] = 1.0f;
    float running = 0.0f;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        const float et = energy[(size_t) (tau + W)] - energy[(size_t) tau];
        const float diff = std::max (0.0f, e0 + et - 2.0f * b[(size_t) tau]);
        running += diff;
        d[(size_t) tau] = running > 0.0f ? diff * tau / running : 1.0f;
    }

    int tau = -1;
    for (int t = std::max (2, tauMin); t < tauMax; ++t)
    {
        if (d[(size_t) t] < threshold)
        {
            while (t + 1 < tauMax && d[(size_t) t + 1] < d[(size_t) t]) ++t;
            tau = t;
            break;
        }
    }
    if (tau < 0)
    {
        tau = std::max (2, tauMin);
        for (int t = tau; t < tauMax; ++t)
            if (d[(size_t) t] < d[(size_t) tau]) tau = t;
    }

    aperiodicity = d[(size_t) tau];
    float p = (float) tau;
    if (tau > 1 && tau < tauMax)
    {
        const float da = d[(size_t) tau - 1], db = d[(size_t) tau], dc = d[(size_t) tau + 1];
        const float denom = da - 2.0f * db + dc;
        if (std::abs (denom) > 1.0e-9f)
            p += std::clamp (0.5f * (da - dc) / denom, -0.5f, 0.5f);
    }
    return p;
}

} // namespace ss::dsp

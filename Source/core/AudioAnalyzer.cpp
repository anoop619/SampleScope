#include "AudioAnalyzer.h"
#include "DSPUtils.h"
#include "Resampler.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <iostream>

namespace ss
{

namespace
{
    inline float clamp01 (float x)                         { return std::clamp (x, 0.0f, 1.0f); }
    inline float ramp (float x, float lo, float hi)        { return clamp01 ((x - lo) / (hi - lo)); }
    inline float fall (float x, float lo, float hi)        { return 1.0f - ramp (x, lo, hi); }
    inline float band (float x, float a, float b, float c, float d) { return std::min (ramp (x, a, b), fall (x, c, d)); }
    inline uint8_t q8 (float x)                            { return (uint8_t) std::lround (clamp01 (x) * 255.0f); }
    inline float toDb (float x)                            { return 20.0f * std::log10 (std::max (x, 1.0e-9f)); }

    /** Raw measurements, before they are condensed into the compact profile. */
    struct Measurements
    {
        float duration = 0, effLength = 0, attackMs = 0, decay20 = 0, endRatio = 0;
        float crestDb = 0, rmsDb = -120, width = 0;
        float centroid = 0, flatness = 0, sub = 0, low = 0, mid = 0, highMid = 0, air = 0;
        float motionOct = 0, centroidSlope = 0, energySlopeDb = 0;
        int   onsetCount = 0;
        float density = 0;
        float harmonicity = 0, pitchConf = 0, f0 = 0, pitchStdCents = 0;
        int   microTransients = 0;
        float tonality = 0;
        float percussiveness = 0;     // HPSS: share of energy in vertical (transient) structures
        float onsetContrast = 0;      // how far onset peaks stand out of the novelty curve
        float acfStrength = 0;
        float tuningCents = 0;
        std::array<float, 12> chroma {}, bassChroma {};
        dsp::OnsetResult onsets;
    };

    //==========================================================================
    void measureEnvelope (const std::vector<float>& x, double sr, Measurements& m, int& startIdx, int& endIdx)
    {
        const int win = 128;
        const int n = (int) x.size();
        const int frames = std::max (1, (n + win - 1) / win);
        std::vector<float> env ((size_t) frames, 0.0f);

        for (int f = 0; f < frames; ++f)
        {
            double s = 0.0; int cnt = 0;
            for (int i = f * win; i < std::min (n, (f + 1) * win); ++i) { s += (double) x[(size_t) i] * x[(size_t) i]; ++cnt; }
            env[(size_t) f] = cnt > 0 ? (float) std::sqrt (s / cnt) : 0.0f;
        }

        const auto peakIt = std::max_element (env.begin(), env.end());
        const float peak = *peakIt;
        const int peakIdx = (int) std::distance (env.begin(), peakIt);
        const double envRate = sr / win;

        int s0 = 0;
        while (s0 < frames - 1 && env[(size_t) s0] < peak * 0.001f) ++s0;
        int e0 = frames - 1;
        while (e0 > s0 && env[(size_t) e0] < peak * 0.01f) --e0;

        startIdx = s0 * win;
        endIdx   = std::min (n, (e0 + 1) * win);
        m.effLength = (float) ((e0 - s0 + 1) / envRate);

        // Attack of the first event: time to reach 90 % of the loudest point within 500 ms.
        const int look = std::min (frames - 1, s0 + (int) (0.5 * envRate));
        float localPeak = 0.0f;
        for (int i = s0; i <= look; ++i) localPeak = std::max (localPeak, env[(size_t) i]);
        int a = s0;
        while (a < look && env[(size_t) a] < 0.9f * localPeak) ++a;
        // Sub-frame refinement with the raw signal.
        float rawPeak = 0.0f; int rawPeakIdx = startIdx;
        for (int i = startIdx; i < std::min (n, (a + 1) * win); ++i)
            if (std::abs (x[(size_t) i]) > rawPeak) { rawPeak = std::abs (x[(size_t) i]); rawPeakIdx = i; }
        m.attackMs = (float) std::max (0.2, (rawPeakIdx - startIdx) * 1000.0 / sr);

        int d = peakIdx;
        while (d < frames - 1 && env[(size_t) d] > peak * 0.1f) ++d;
        m.decay20 = (float) ((d - peakIdx) / envRate);

        // Level at the very end of the file (loops keep going until the last sample).
        const int tailFrames = std::max (1, frames / 20);
        float tail = 0.0f;
        for (int i = frames - tailFrames; i < frames; ++i) tail += env[(size_t) i];
        m.endRatio = peak > 0 ? (tail / tailFrames) / peak : 0.0f;

        // Crest factor and level over the audible part.
        double sumSq = 0.0; float maxAbs = 0.0f;
        for (int i = startIdx; i < endIdx; ++i) { sumSq += (double) x[(size_t) i] * x[(size_t) i]; maxAbs = std::max (maxAbs, std::abs (x[(size_t) i])); }
        const float rms = (float) std::sqrt (sumSq / std::max (1, endIdx - startIdx));
        m.crestDb = toDb (maxAbs) - toDb (rms);

        // Micro-transients in the first 60 ms (the "flam" of a clap): smoothed power envelope,
        // count bursts separated by deep dips.
        {
            const double smoothing = std::exp (-1.0 / (0.003 * sr));
            const int step = std::max (1, (int) (0.0005 * sr));
            const int stop = std::min (n, startIdx + (int) (0.06 * sr));
            std::vector<float> e;
            double pw = 0.0;
            for (int i = startIdx; i < stop; ++i)
            {
                pw = smoothing * pw + (1.0 - smoothing) * (double) x[(size_t) i] * x[(size_t) i];
                if ((i - startIdx) % step == 0) e.push_back ((float) std::sqrt (pw));
            }
            int count = 0;
            if (e.size() > 8)
            {
                const float mx = *std::max_element (e.begin(), e.end());
                const int nb = std::max (2, (int) (0.002 * sr / step));
                float lastPeak = -1.0f, dip = 1.0e9f;
                for (int k = 1; k + 1 < (int) e.size(); ++k)
                {
                    dip = std::min (dip, e[(size_t) k]);
                    bool isMax = e[(size_t) k] > 0.25f * mx;
                    for (int j = std::max (0, k - nb); j <= std::min ((int) e.size() - 1, k + nb) && isMax; ++j)
                        if (e[(size_t) j] > e[(size_t) k]) isMax = false;
                    if (! isMax) continue;
                    if (lastPeak < 0.0f || dip < 0.5f * std::min (lastPeak, e[(size_t) k]))
                    {
                        ++count;
                        lastPeak = e[(size_t) k];
                        dip = 1.0e9f;
                    }
                }
            }
            m.microTransients = count;
        }
    }

    //==========================================================================
    /** Median of a small strided set of values (in-place partial sort on a scratch buffer). */
    inline float medianOf (std::vector<float>& scratch)
    {
        auto mid = scratch.begin() + (long) scratch.size() / 2;
        std::nth_element (scratch.begin(), mid, scratch.end());
        return *mid;
    }

    void measureSpectrum (const std::vector<float>& x, double sr, int start, int end, Measurements& m)
    {
        dsp::Spectrum spec (11);
        const int N = spec.size(), hop = N / 4, bins = spec.numBins();
        const double binHz = sr / N;

        auto binOf = [&] (double hz) { return std::clamp ((int) std::round (hz / binHz), 0, bins - 1); };
        const int bSub = binOf (90), bLow = binOf (300), bMid = binOf (2000), bHm = binOf (6000);
        const int hpssBins = binOf (8000) + 1;

        double totalW = 0, centroidAcc = 0, flatAcc = 0, flatW = 0;
        double bands[5] = {};
        std::vector<std::pair<float, float>> centroidTrack;   // (log2 centroid, weight)
        std::vector<float> frameEnergyDb;
        std::vector<float> spectrogram;                        // frames x hpssBins, for HPSS
        float maxFrameEnergy = 0.0f;

        std::vector<float> frame ((size_t) N), cum ((size_t) bins);
        int numFrames = 0;
        for (int pos = start; pos < std::max (end, start + 1); pos += hop)
        {
            const int avail = std::min (N, (int) x.size() - pos);
            if (avail <= 0) break;
            std::copy (x.begin() + pos, x.begin() + pos + avail, frame.begin());
            const float* mags = spec.compute (frame.data(), avail);

            spectrogram.insert (spectrogram.end(), mags, mags + hpssBins);
            ++numFrames;

            double p = 0, pf = 0;
            double b[5] = {};
            for (int k = 1; k < bins; ++k)
            {
                const double pw = (double) mags[k] * mags[k];
                p += pw; pf += pw * k * binHz;
                cum[(size_t) k] = (float) p;
                const int bi = k < bSub ? 0 : k < bLow ? 1 : k < bMid ? 2 : k < bHm ? 3 : 4;
                b[bi] += pw;
            }
            if (p <= 1.0e-12) { frameEnergyDb.push_back (-120.0f); continue; }

            // Flatness inside the band that actually holds the energy (5 %..95 %), so band-limited
            // noise (hats, shakers) reads as noisy and a low sine does not read as "flat".
            int kLo = 1, kHi = bins - 1;
            while (kLo < bins - 1 && cum[(size_t) kLo] < 0.05 * p) ++kLo;
            while (kHi > kLo && cum[(size_t) kHi - 1] > 0.95 * p) --kHi;
            if (kHi - kLo < 16) { const int c = (kLo + kHi) / 2; kLo = std::max (1, c - 8); kHi = std::min (bins - 1, c + 8); }

            double logSum = 0, linSum = 0; int cnt = 0;
            for (int k = kLo; k <= kHi; ++k)
            {
                const double pw = (double) mags[k] * mags[k] + 1.0e-12;
                logSum += std::log (pw); linSum += pw; ++cnt;
            }
            const double flat = cnt > 0 ? std::exp (logSum / cnt) / (linSum / cnt) : 0.0;

            const double c = pf / p;
            totalW += p; centroidAcc += c * p;
            flatAcc += flat * std::sqrt (p); flatW += std::sqrt (p);
            for (int i = 0; i < 5; ++i) bands[i] += b[i];

            centroidTrack.push_back ({ (float) std::log2 (std::max (c, 20.0)), (float) p });
            frameEnergyDb.push_back (10.0f * std::log10 ((float) p));
            maxFrameEnergy = std::max (maxFrameEnergy, (float) p);
        }

        if (totalW <= 0)
            return;

        m.centroid = (float) (centroidAcc / totalW);
        m.flatness = (float) (flatW > 0 ? flatAcc / flatW : 0);
        const double bandTotal = bands[0] + bands[1] + bands[2] + bands[3] + bands[4];
        m.sub = (float) (bands[0] / bandTotal); m.low = (float) (bands[1] / bandTotal);
        m.mid = (float) (bands[2] / bandTotal); m.highMid = (float) (bands[3] / bandTotal);
        m.air = (float) (bands[4] / bandTotal);

        // Harmonic/percussive separation (median filtering, Fitzgerald 2010).
        // Horizontal lines (sustained partials) vs vertical lines (transients).
        {
            const int tLen = std::max (3, (int) std::lround (0.2 * sr / hop)) | 1;   // ~200 ms
            const int fLen = 17;                                                    // ~180 Hz
            const int hT = tLen / 2, hF = fLen / 2;
            std::vector<float> scratch;
            double percE = 0, allE = 0;
            const int hpssFrames = std::min (numFrames, (int) (8.0 * sr / hop));   // a representative stretch
            for (int t = 0; t < hpssFrames; ++t)
            {
                const float* row = spectrogram.data() + (size_t) t * (size_t) hpssBins;
                for (int k = 1; k < hpssBins; ++k)
                {
                    const float X = row[k];
                    if (X <= 1.0e-6f) continue;

                    scratch.clear();
                    for (int j = t - hT; j <= t + hT; ++j)
                        scratch.push_back (j >= 0 && j < numFrames ? spectrogram[(size_t) j * (size_t) hpssBins + (size_t) k] : 0.0f);
                    const float H = medianOf (scratch);

                    scratch.clear();
                    for (int j = k - hF; j <= k + hF; ++j)
                        scratch.push_back (j >= 1 && j < hpssBins ? row[j] : 0.0f);
                    const float P = medianOf (scratch);

                    const double e = (double) X;                       // magnitude weighting
                    const double h2 = (double) H * H, p2 = (double) P * P;
                    allE += e;
                    if (h2 + p2 > 0) percE += e * p2 / (h2 + p2);
                }
            }
            m.percussiveness = allE > 0 ? (float) (percE / allE) : 0.0f;
        }

        // Spectral motion: spread and trend of the centroid over the frames that matter.
        const float floor = maxFrameEnergy * 1.0e-3f;
        double sw = 0, sx = 0, st = 0;
        for (size_t i = 0; i < centroidTrack.size(); ++i)
            if (centroidTrack[i].second > floor) { sw += 1; sx += centroidTrack[i].first; st += (double) i; }
        if (sw >= 3)
        {
            const double mx = sx / sw, mt = st / sw;
            double var = 0, cov = 0, vt = 0;
            for (size_t i = 0; i < centroidTrack.size(); ++i)
            {
                if (centroidTrack[i].second <= floor) continue;
                const double dx = centroidTrack[i].first - mx, dt = (double) i - mt;
                var += dx * dx; cov += dx * dt; vt += dt * dt;
            }
            m.motionOct = (float) std::sqrt (var / sw);
            const double framesPerSec = sr / hop;
            m.centroidSlope = vt > 0 ? (float) (cov / vt * framesPerSec) : 0.0f;

            // Energy trend (dB/s) - risers get louder, impacts decay.
            double ce = 0, me = 0; int ne = 0;
            for (auto e : frameEnergyDb) if (e > -100) { me += e; ++ne; }
            if (ne >= 3)
            {
                me /= ne;
                double mt2 = 0; int k = 0;
                for (size_t i = 0; i < frameEnergyDb.size(); ++i) if (frameEnergyDb[i] > -100) { mt2 += (double) i; ++k; }
                mt2 /= k;
                double vt2 = 0;
                for (size_t i = 0; i < frameEnergyDb.size(); ++i)
                {
                    if (frameEnergyDb[i] <= -100) continue;
                    ce += (frameEnergyDb[i] - me) * ((double) i - mt2);
                    vt2 += ((double) i - mt2) * ((double) i - mt2);
                }
                m.energySlopeDb = vt2 > 0 ? (float) (ce / vt2 * framesPerSec) : 0.0f;
            }
        }
    }

    //==========================================================================
    void measureTonality (const std::vector<float>& x, double sr, int start, int end, Measurements& m)
    {
        dsp::Spectrum spec (13);   // 8192 @ 22.05 kHz: 2.7 Hz bins, good enough for bass notes
        const int N = spec.size(), hop = 2048, bins = spec.numBins();
        const double binHz = sr / N;
        const int k0 = std::max (1, (int) (30.0 / binHz)), k1 = std::min (bins - 2, (int) (4000.0 / binHz));

        std::vector<dsp::Peak> allPeaks, framePeaks;
        std::vector<std::pair<size_t, size_t>> frameRanges;
        double harmAcc = 0, harmW = 0;

        std::vector<float> frame ((size_t) N);
        for (int pos = start; pos < std::max (end, start + 1); pos += hop)
        {
            const int avail = std::min (N, (int) x.size() - pos);
            if (avail <= 0) break;
            std::copy (x.begin() + pos, x.begin() + pos + avail, frame.begin());
            const float* mags = spec.compute (frame.data(), avail);

            double total = 0;
            for (int k = k0; k <= k1; ++k) total += (double) mags[k] * mags[k];
            if (total < 1.0e-6) continue;

            dsp::findPeaks (mags, bins, binHz, 30.0f, 4000.0f, 0.02f, 24, framePeaks);
            double inPeaks = 0;
            for (const auto& p : framePeaks)
            {
                const int k = (int) std::lround (p.freq / binHz);
                for (int j = std::max (k0, k - 2); j <= std::min (k1, k + 2); ++j)
                    inPeaks += (double) mags[j] * mags[j];
            }
            const double h = std::min (1.0, inPeaks / total);
            harmAcc += h * std::sqrt (total); harmW += std::sqrt (total);

            frameRanges.push_back ({ allPeaks.size(), allPeaks.size() + framePeaks.size() });
            allPeaks.insert (allPeaks.end(), framePeaks.begin(), framePeaks.end());
        }

        m.harmonicity = harmW > 0 ? (float) (harmAcc / harmW) : 0.0f;
        m.tuningCents = dsp::estimateTuningCents (allPeaks);
        m.chroma.fill (0.0f); m.bassChroma.fill (0.0f);
        dsp::addPeaksToChroma (allPeaks, m.tuningCents, m.chroma, &m.bassChroma);
    }

    //==========================================================================
    void measurePitch (const std::vector<float>& x, double sr, int start, int end, float attackMs, Measurements& m)
    {
        const int W = 1024;
        const int tauMax = (int) (sr / 32.0);
        const int tauMin = (int) (sr / 1600.0);
        const int need = W + tauMax + 2;
        int pos = start + (int) ((attackMs + 15.0f) * 0.001 * sr);
        const int stop = std::min (end, start + (int) (1.2 * sr));

        std::vector<float> f0s;
        float aperSum = 0.0f;
        int frames = 0;
        dsp::Yin yin (W, tauMax);
        for (; pos + need <= (int) x.size() && pos < stop && frames < 20; pos += 512, ++frames)
        {
            float aper = 1.0f;
            const float period = yin.period (x.data() + pos, tauMin, 0.15f, aper);
            aperSum += aper;
            if (period > 0.0f && aper < 0.25f)
                f0s.push_back ((float) (sr / period));
        }

        if (frames == 0 || f0s.empty())
            return;

        std::vector<float> sorted = f0s;
        std::nth_element (sorted.begin(), sorted.begin() + (long) sorted.size() / 2, sorted.end());
        m.f0 = sorted[sorted.size() / 2];
        m.pitchConf = clamp01 ((float) f0s.size() / frames) * clamp01 (1.0f - aperSum / frames);

        double var = 0.0;
        for (auto f : f0s) { const double c = 1200.0 * std::log2 (f / m.f0); var += c * c; }
        m.pitchStdCents = (float) std::sqrt (var / f0s.size());
    }

    //==========================================================================
    /** Loop decision and tempo. Loops in packs are cut to whole bars, which is the strongest clue. */
    void decideLoopAndTempo (Measurements& m, const NameHints& hints, AnalysisResult& r)
    {
        float acfStrength = 0.0f;
        const float acfBpm = m.duration >= 1.0f ? dsp::estimateTempo (m.onsets, 60.0f, 200.0f, &acfStrength) : 0.0f;
        m.acfStrength = acfStrength;

        // Candidate tempi implied by the file length.
        struct Cand { float bpm; int beats; };
        std::vector<Cand> cands;
        if (m.duration >= 0.9f)
            for (int beats : { 4, 8, 16, 32, 64, 2, 12, 24, 48, 6, 3 })
            {
                const float b = 60.0f * beats / m.duration;
                if (b >= 62.0f && b <= 200.0f) cands.push_back ({ b, beats });
            }

        auto octaveDistance = [] (float a, float b)
        {
            float d = std::abs (std::log2 (a / b));
            d = std::min (d, std::abs (std::log2 (a / (b * 2.0f))) + 0.01f);
            d = std::min (d, std::abs (std::log2 (a / (b * 0.5f))) + 0.01f);
            return d;
        };

        const Cand* fit = nullptr;
        float fitDist = 1.0e9f;
        if (acfBpm > 0.0f)
            for (const auto& c : cands)
            {
                const float d = octaveDistance (c.bpm, acfBpm) + (c.beats % 4 == 0 ? 0.0f : 0.01f);
                if (d < fitDist) { fitDist = d; fit = &c; }
            }
        const bool barFit = fit != nullptr && fitDist < 0.04f;

        const Cand* nameFit = nullptr;
        if (hints.bpm > 0.0f)
            for (const auto& c : cands)
                if (std::abs (std::log2 (c.bpm / hints.bpm)) < 0.012f) { nameFit = &c; break; }

        // --- loop or one-shot? ---------------------------------------------------
        if (hints.loopHint > 0)       r.isLoop = true;
        else if (hints.loopHint < 0)  r.isLoop = false;
        else if (hints.bpmExplicit && m.duration >= 1.0f) r.isLoop = true;
        else
        {
            const bool enoughEvents = m.onsetCount >= 4 && m.onsetContrast > 4.0f;
            const bool rhythmic = acfStrength > 0.15f;
            const bool playsToEnd = m.endRatio > 0.04f;
            r.isLoop = m.duration >= 1.0f && enoughEvents
                       && ((barFit && rhythmic) || nameFit != nullptr || (m.density >= 1.5f && rhythmic))
                       && (playsToEnd || barFit || nameFit != nullptr);
        }

        if (! r.isLoop)
            return;

        // --- tempo -----------------------------------------------------------------
        float bpm = 0.0f;
        if (hints.bpmExplicit)                          { bpm = hints.bpm; r.bpmFromName = true; }
        else if (nameFit != nullptr)                    { bpm = hints.bpm; r.bpmFromName = true; }
        else if (barFit)                                bpm = fit->bpm;
        else if (hints.bpm > 0.0f && acfBpm > 0.0f && octaveDistance (hints.bpm, acfBpm) < 0.04f) { bpm = hints.bpm; r.bpmFromName = true; }
        else if (acfBpm > 0.0f)                         bpm = std::round (acfBpm * 2.0f) / 2.0f;

        // Fold into the range producers think in.
        while (bpm > 0.0f && bpm < 65.0f)  bpm *= 2.0f;
        while (bpm > 200.0f)               bpm *= 0.5f;

        if (std::abs (bpm - std::round (bpm)) < 0.06f) bpm = std::round (bpm);
        r.bpm = bpm;
    }

    //==========================================================================
    std::array<float, numCategories> audioCategoryScores (const Measurements& m, bool isLoop)
    {
        std::array<float, numCategories> s {};
        auto set = [&s] (Category c, float v) { s[(size_t) c] = std::max (s[(size_t) c], v); };

        const float L = m.effLength, A = m.attackMs, C = m.centroid, F = m.flatness, T = m.tonality;
        const float lows = m.sub + m.low;
        const float fast = fall (A, 12.0f, 40.0f);

        set (Category::Unknown, 0.12f);

        if (! isLoop)
        {
            set (Category::Kick, fast * fall (L, 0.9f, 2.0f) * ramp (lows, 0.45f, 0.75f) * fall (C, 500.0f, 1500.0f)
                                 * (1.0f - 0.6f * ramp (L, 0.5f, 1.2f) * ramp (T, 0.55f, 0.8f)));

            set (Category::Sub808, fall (A, 20.0f, 60.0f) * ramp (L, 0.45f, 1.0f) * ramp (lows, 0.6f, 0.85f)
                                   * ramp (T, 0.45f, 0.7f) * (m.f0 > 0 ? fall (m.f0, 90.0f, 140.0f) : 0.6f));

            set (Category::Bass, 0.85f * ramp (T, 0.45f, 0.7f) * (m.f0 > 0 ? band (m.f0, 25, 30, 200, 330) : 0.5f)
                                 * ramp (lows + 0.5f * m.mid, 0.55f, 0.8f) * fall (C, 900.0f, 2500.0f) * ramp (L, 0.12f, 0.3f));

            const float clapness = ramp ((float) m.microTransients, 1.5f, 2.5f);
            set (Category::Snare, fast * fall (L, 0.6f, 1.4f) * ramp (F, 0.08f, 0.25f) * band (C, 600, 1200, 5000, 8000)
                                  * fall (m.sub, 0.25f, 0.45f) * ramp (m.mid + m.highMid, 0.3f, 0.55f) * (1.0f - 0.6f * clapness));

            set (Category::Clap, fast * fall (L, 0.6f, 1.4f) * ramp (F, 0.1f, 0.3f) * band (C, 600, 1000, 4500, 7000)
                                 * fall (lows, 0.12f, 0.3f) * clapness);

            set (Category::HiHat, fall (A, 10.0f, 40.0f) * ramp (C, 3500.0f, 5500.0f) * ramp (m.highMid + m.air, 0.55f, 0.75f)
                                  * fall (L, 0.7f, 1.6f) * ramp (F, 0.1f, 0.3f));

            set (Category::Cymbal, ramp (C, 3000.0f, 5000.0f) * ramp (m.highMid + m.air, 0.5f, 0.7f) * ramp (L, 0.9f, 1.6f)
                                   * ramp (F, 0.1f, 0.3f) * fall (A, 20.0f, 80.0f) * fall (m.energySlopeDb, -2.0f, 2.0f));

            set (Category::Tom, fast * band (C, 150, 250, 900, 1500) * ramp (T, 0.35f, 0.55f)
                                * (m.f0 > 0 ? band (m.f0, 60, 80, 300, 400) : 0.3f) * band (L, 0.1f, 0.2f, 0.9f, 1.5f)
                                * fall (lows, 0.7f, 0.9f));

            set (Category::Percussion, 0.4f * fast * fall (L, 0.5f, 1.2f));

            set (Category::Pad, ramp (A, 40.0f, 150.0f) * ramp (L, 1.0f, 2.5f) * ramp (T, 0.4f, 0.65f));

            set (Category::Pluck, fast * ramp (T, 0.45f, 0.7f) * band (L, 0.08f, 0.15f, 1.2f, 2.5f)
                                  * (m.f0 > 0 ? ramp (m.f0, 100.0f, 150.0f) : 0.6f) * fall (lows, 0.6f, 0.85f));

            set (Category::Lead, 0.9f * ramp (T, 0.5f, 0.75f) * ramp (L, 0.5f, 1.2f) * fall (A, 60.0f, 200.0f) * ramp (C, 500.0f, 1200.0f));

            set (Category::Vocal, ramp (T, 0.3f, 0.45f) * (m.f0 > 0 ? band (m.f0, 80, 100, 800, 1000) : 0.3f)
                                  * ramp (m.pitchStdCents, 15.0f, 45.0f) * band (C, 300, 500, 3000, 5000) * ramp (L, 0.25f, 0.6f)
                                  * fall (m.percussiveness, 0.3f, 0.6f));

            set (Category::Melodic, 0.5f * ramp (T, 0.5f, 0.8f));
        }
        else
        {
            const float percussive = std::max (fall (T, 0.4f, 0.6f), ramp (m.percussiveness, 0.35f, 0.55f))
                                     * ramp (m.density, 1.0f, 2.5f);
            set (Category::Drums, percussive * ramp (lows, 0.12f, 0.3f));
            const float topLoop = percussive * fall (lows, 0.12f, 0.3f);
            const float hatness = ramp (C, 4000.0f, 6000.0f);
            set (Category::Percussion, topLoop * (1.0f - hatness));
            set (Category::HiHat, topLoop * hatness);

            set (Category::Bass, ramp (T, 0.4f, 0.6f) * ramp (lows, 0.5f, 0.75f) * fall (C, 500.0f, 1200.0f)
                                 * fall (m.mid + m.highMid, 0.12f, 0.3f) * fall (m.percussiveness, 0.35f, 0.55f));
            set (Category::Melodic, 0.9f * ramp (T, 0.4f, 0.65f) * fall (m.percussiveness, 0.35f, 0.6f));
            set (Category::Pad, ramp (T, 0.45f, 0.65f) * fall (m.density, 0.6f, 1.5f) * ramp (A, 30.0f, 120.0f));
            set (Category::Vocal, 0.35f * band (T, 0.3f, 0.45f, 0.75f, 0.9f) * band (C, 500, 800, 2500, 4000) * fall (lows, 0.3f, 0.5f));
        }

        // FX and textures look alike either way.
        const float moving = std::max (ramp (m.motionOct, 0.25f, 0.6f), ramp (std::abs (m.centroidSlope), 0.4f, 1.0f));
        const float impact = fast * ramp (L, 1.0f, 2.0f) * fall (T, 0.4f, 0.6f) * ramp (F, 0.1f, 0.3f) * (isLoop ? 0.0f : 1.0f);
        const float rhythmic = isLoop ? ramp (m.acfStrength, 0.3f, 0.6f) : 0.0f;
        set (Category::FX, std::max (ramp (L, 0.8f, 2.0f) * moving * fall (T, 0.6f, 0.85f) * (1.0f - rhythmic), 0.8f * impact));
        set (Category::Texture, ramp (L, 1.8f, 4.0f) * fall (m.density, 1.0f, 2.5f) * ramp (A, 20.0f, 100.0f) * fall (T, 0.55f, 0.75f));

        return s;
    }

    //==========================================================================
    uint32_t computeTags (const Measurements& m, Category c, bool isLoop, int numChannels, uint8_t punchQ)
    {
        uint32_t t = 0;
        const bool lowGroup  = c == Category::Kick || c == Category::Sub808 || c == Category::Bass || c == Category::Tom;
        const bool highGroup = c == Category::HiHat || c == Category::Cymbal;
        const bool drums = isDrumCategory (c);

        const float brightHz = lowGroup ? 900.0f : highGroup ? 7000.0f : 2800.0f;
        const float darkHz   = lowGroup ? 250.0f : highGroup ? 4500.0f : 700.0f;
        if (m.centroid > brightHz) t |= tagBright;
        if (m.centroid < darkHz)   t |= tagDark;

        if (punchQ > 140 && m.attackMs < 15.0f)                            t |= tagPunchy;
        if (m.attackMs > 40.0f || (! isLoop && m.crestDb < 8.0f && ! drums)) t |= tagSoft;
        if (m.low + m.mid > 0.55f && m.air < 0.04f && m.tonality > 0.3f)  t |= tagWarm;
        if (m.air > 0.12f && ! highGroup)                                  t |= tagAiry;
        if (m.sub > 0.35f)                                                 t |= tagDeep;
        if (m.tonality > 0.35f && m.flatness > 0.12f && m.highMid + m.air > 0.2f && ! highGroup) t |= tagGritty;
        if (m.flatness > 0.35f)                                            t |= tagNoisy;
        if (m.flatness < 0.05f && m.tonality > 0.6f)                       t |= tagClean;
        if (m.tonality > 0.6f && (! drums || c == Category::Tom))           t |= tagTonal;

        if (! isLoop)
        {
            const float shortL = drums ? 0.15f : 0.3f;
            const float longL  = drums ? 0.8f : (c == Category::FX || c == Category::Texture) ? 3.0f : 2.0f;
            if (m.effLength < shortL) t |= tagShort;
            if (m.effLength > longL)  t |= tagLong;
            if (drums && m.decay20 < 0.12f) t |= tagTight;
        }
        else
        {
            if (m.density > 5.0f) t |= tagBusy;
            if (m.density < 1.5f) t |= tagSparse;
        }

        if (numChannels > 1 && m.width > 0.3f)        t |= tagWide;
        if (numChannels == 1 || m.width < 0.03f)      t |= tagMono;
        if (lowGroup && m.sub + m.low > 0.7f && m.decay20 > 0.35f) t |= tagBoomy;

        if (m.effLength > 1.0f && ! drums && (m.centroidSlope > 0.35f || m.energySlopeDb > 4.0f)) t |= tagRising;
        if (m.effLength > 1.0f && m.centroidSlope < -0.35f && (c == Category::FX || c == Category::Texture)) t |= tagFalling;

        if (m.rmsDb > -12.0f) t |= tagLoud;
        if (m.rmsDb < -30.0f) t |= tagQuiet;
        if (m.effLength > 2.0f && m.motionOct > 0.35f && ! drums && m.density < 3.0f) t |= tagEvolving;
        return t;
    }
}

//==============================================================================
AudioAnalyzer::AudioAnalyzer()
{
    formats.registerBasicFormats();
}

AnalysisResult AudioAnalyzer::analyzeFile (const juce::File& file)
{
    AnalysisResult fail;
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        fail.error = "Unsupported or unreadable file";
        return fail;
    }

    const double sr = reader->sampleRate;
    const auto totalLen = reader->lengthInSamples;
    if (sr <= 0.0 || totalLen <= 0)
    {
        fail.error = "Empty file";
        return fail;
    }

    const int channels = (int) juce::jlimit (1u, 2u, reader->numChannels);
    const auto toRead = (int) std::min<juce::int64> (totalLen, (juce::int64) (maxAnalysisSeconds * sr));

    std::vector<float> mono ((size_t) toRead);
    juce::AudioBuffer<float> chunk (channels, 65536);
    double midE = 0.0, sideE = 0.0;

    for (int pos = 0; pos < toRead;)
    {
        const int n = std::min (65536, toRead - pos);
        reader->read (&chunk, 0, n, pos, true, channels > 1);
        const float* l = chunk.getReadPointer (0);
        const float* r = chunk.getReadPointer (channels > 1 ? 1 : 0);
        for (int i = 0; i < n; ++i)
        {
            const float mid = 0.5f * (l[i] + r[i]);
            const float side = 0.5f * (l[i] - r[i]);
            mono[(size_t) (pos + i)] = mid;
            midE += (double) mid * mid; sideE += (double) side * side;
        }
        pos += n;
    }

    const float width = (midE + sideE) > 0 ? (float) std::sqrt (sideE / (midE + sideE)) : 0.0f;
    auto result = analyzeMono (mono, sr, (double) totalLen / sr, (int) reader->numChannels, width, parseNameHints (file));
    result.sampleRate = (uint32_t) sr;
    return result;
}

AnalysisResult AudioAnalyzer::analyzeBuffer (const juce::AudioBuffer<float>& buffer, double sampleRate, const NameHints& hints)
{
    const int n = std::min (buffer.getNumSamples(), (int) (maxAnalysisSeconds * sampleRate));
    const int ch = buffer.getNumChannels();
    std::vector<float> mono ((size_t) n, 0.0f);
    double midE = 0.0, sideE = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const float l = buffer.getSample (0, i);
        const float r = buffer.getSample (ch > 1 ? 1 : 0, i);
        mono[(size_t) i] = 0.5f * (l + r);
        midE += 0.25 * (l + r) * (l + r);
        sideE += 0.25 * (l - r) * (l - r);
    }
    const float width = (midE + sideE) > 0 ? (float) std::sqrt (sideE / (midE + sideE)) : 0.0f;
    auto r = analyzeMono (mono, sampleRate, buffer.getNumSamples() / sampleRate, ch, width, hints);
    r.sampleRate = (uint32_t) sampleRate;
    return r;
}

AnalysisResult AudioAnalyzer::analyzeMono (std::vector<float>& mono, double sr, double fullDuration,
                                           int numChannels, float stereoWidth, const NameHints& hints)
{
    AnalysisResult r;
    r.ok = true;
    r.duration = fullDuration;
    r.numChannels = (uint8_t) juce::jlimit (1, 255, numChannels);

    // 1. Resample to the analysis rate and free the original right away.
    std::vector<float> x = std::abs (sr - analysisRate) < 1.0
                               ? std::move (mono)
                               : Resampler::resample (mono.data(), (int) mono.size(), sr / analysisRate, 8);
    std::vector<float>().swap (mono);
    const double asr = analysisRate;

    Measurements m;
    m.duration = (float) fullDuration;
    m.width = stereoWidth;

    float peak = 0.0f; double sumSq = 0.0;
    for (auto v : x) { peak = std::max (peak, std::abs (v)); sumSq += (double) v * v; }

    if (x.size() < 64 || peak < 1.0e-5f)
    {
        // Silence: only the name can tell us anything.
        int best = 0;
        for (int c = 1; c < numCategories; ++c)
            if (hints.categoryScore[(size_t) c] > hints.categoryScore[(size_t) best]) best = c;
        r.category = (Category) best;
        r.key = hints.key; r.keyFromName = hints.key != Key::none;
        r.tags = tagQuiet;
        return r;
    }

    // Level is measured before normalising; everything else after.
    int startIdx = 0, endIdx = (int) x.size();
    {
        // RMS over the audible part (computed after envelope bounds below).
    }
    for (auto& v : x) v /= peak;

    const bool dbg = juce::SystemStats::getEnvironmentVariable ("SS_DEBUG", {}) == "2";
    auto tick = juce::Time::getMillisecondCounterHiRes();
    auto lap = [&] (const char* what) { if (dbg) { auto now = juce::Time::getMillisecondCounterHiRes(); std::cerr << what << "=" << (now - tick) << "ms "; tick = now; } };
    lap ("resample+norm");
    measureEnvelope (x, asr, m, startIdx, endIdx);
    lap ("env");
    {
        double s = 0.0;
        for (int i = startIdx; i < endIdx; ++i) s += (double) x[(size_t) i] * x[(size_t) i];
        const float rmsNorm = (float) std::sqrt (s / std::max (1, endIdx - startIdx));
        m.rmsDb = toDb (rmsNorm * peak);
    }

    measureSpectrum (x, asr, startIdx, endIdx, m);
    lap ("spectrum+hpss");

    m.onsets = dsp::detectOnsets (x.data(), (int) x.size(), asr);
    for (auto o : m.onsets.onsets)
        if (o >= startIdx - 256 && o <= endIdx) ++m.onsetCount;
    m.density = m.onsetCount / std::max (0.05f, m.effLength);
    {
        // How far the onset peaks stand out of the novelty curve: high for real hits,
        // low for vibrato, beating detuned saws and evolving noise.
        auto env = m.onsets.envelope;
        if (! env.empty() && ! m.onsets.onsets.empty())
        {
            float peakSum = 0.0f;
            for (auto o : m.onsets.onsets) peakSum += env[(size_t) std::min ((int) env.size() - 1, o / m.onsets.hop)];
            auto mid = env.begin() + (long) env.size() / 2;
            std::nth_element (env.begin(), mid, env.end());
            m.onsetContrast = (peakSum / (float) m.onsets.onsets.size()) / std::max (0.01f, *mid);
        }
    }

    lap ("onsets");
    measureTonality (x, asr, startIdx, endIdx, m);
    lap ("tonal");

    // Loop / tempo first: pitch tracking only makes sense for one-shots.
    decideLoopAndTempo (m, hints, r);

    lap ("tempo");
    if (! r.isLoop)
        measurePitch (x, asr, startIdx, endIdx, m.attackMs, m);
    lap ("pitch");
    if (dbg) std::cerr << "\n";

    if (juce::SystemStats::getEnvironmentVariable ("SS_DEBUG", {}) == "1")
        std::cerr << "    L=" << m.effLength << " A=" << m.attackMs << " dec=" << m.decay20 << " end=" << m.endRatio
                  << " crest=" << m.crestDb << " C=" << m.centroid << " F=" << m.flatness
                  << " sub=" << m.sub << " low=" << m.low << " mid=" << m.mid << " hm=" << m.highMid << " air=" << m.air
                  << " harm=" << m.harmonicity << " perc=" << m.percussiveness << " pc=" << m.pitchConf << " f0=" << m.f0
                  << " pstd=" << m.pitchStdCents << " on=" << m.onsetCount << " dens=" << m.density
                  << " contrast=" << m.onsetContrast << " acf=" << m.acfStrength << " micro=" << m.microTransients
                  << " mot=" << m.motionOct << " slope=" << m.centroidSlope << " eslope=" << m.energySlopeDb << "\n";

    const float noiseFactor = clamp01 (ramp (std::log10 (std::max (m.flatness, 1.0e-4f)), -2.0f, -0.3f));
    m.tonality = r.isLoop ? clamp01 (m.harmonicity * (1.0f - 0.4f * noiseFactor))
                          : clamp01 (0.6f * m.harmonicity + 0.4f * m.pitchConf) * (1.0f - 0.4f * noiseFactor);
    m.tonality *= 1.0f - 0.6f * ramp (m.percussiveness, 0.25f, 0.75f);

    // Onsets only count as rhythm when they stand out (vibrato and beating detuned saws don't).
    if (m.onsetContrast < 4.0f)
        m.density *= ramp (m.onsetContrast, 1.5f, 4.0f);

    // 2. Category: audio evidence + name evidence.
    auto scores = audioCategoryScores (m, r.isLoop);
    for (int c = 0; c < numCategories; ++c)
        scores[(size_t) c] += hints.categoryScore[(size_t) c];

    int best = 0;
    for (int c = 1; c < numCategories; ++c)
        if (scores[(size_t) c] > scores[(size_t) best]) best = c;
    r.category = (Category) best;
    if (! r.isLoop && r.category == Category::Drums)
        r.category = Category::Percussion;

    // 3. Key / root note.
    const bool pitched = isPitchedCategory (r.category);
    const bool tunedDrum = r.category == Category::Kick || r.category == Category::Tom;

    if (hints.key != Key::none)
    {
        r.key = hints.key; r.keyFromName = true; r.keyConfidence = 1.0f;
    }

    if (! r.isLoop && (pitched || tunedDrum) && m.f0 > 0.0f && m.pitchConf > (tunedDrum ? 0.6f : 0.45f)
        && dsp::chromaPeakiness (m.chroma) > 0.12f)
    {
        const int note = (int) std::lround (dsp::hzToMidi (m.f0) - m.tuningCents / 100.0f);
        if (note >= 0 && note < 128)
        {
            // A single dominant pitch: store it as the root. Chords fall through to key detection.
            std::array<float, 12> c = m.chroma;
            std::sort (c.begin(), c.end(), std::greater<float>());
            const bool chordLike = c[1] > 0.45f * c[0] && c[2] > 0.35f * c[0];
            if (! chordLike || r.category == Category::Sub808 || r.category == Category::Bass || tunedDrum)
                r.rootNote = note;
        }
    }

    if (! r.keyFromName && pitched && r.rootNote < 0)
    {
        std::array<float, 12> chroma = m.chroma;
        for (int i = 0; i < 12; ++i) chroma[(size_t) i] += 0.5f * m.bassChroma[(size_t) i];

        float conf = 0.0f;
        int key = Key::none;
        if (! r.isLoop && m.effLength < 2.5f)
        {
            key = dsp::estimateTriad (m.chroma, conf);    // chord stab
            if (conf < 0.6f) key = Key::none;
        }
        else
        {
            key = dsp::estimateKey (chroma, conf);
        }

        if (key != Key::none && m.tonality > 0.3f)
        {
            r.key = key;
            r.keyConfidence = conf;
        }
    }

    // 4. Compact profile.
    auto& f = r.features;
    f[fBrightness] = q8 (std::log2 (std::max (m.centroid, 100.0f) / 100.0f) / std::log2 (80.0f));
    f[fNoisiness]  = q8 (noiseFactor);
    f[fPunch]      = q8 (ramp (m.crestDb, 6.0f, 22.0f) * fall (m.attackMs, 5.0f, 60.0f));
    f[fAttack]     = q8 (ramp (std::log10 (std::max (m.attackMs, 0.5f)), std::log10 (0.5f), 3.0f));
    f[fLength]     = q8 (ramp (std::log10 (std::max (m.effLength, 0.05f)), std::log10 (0.05f), std::log10 (20.0f)));
    f[fSub]        = q8 (std::sqrt (m.sub));
    f[fLow]        = q8 (std::sqrt (m.low));
    f[fMid]        = q8 (std::sqrt (m.mid));
    f[fHighMid]    = q8 (std::sqrt (m.highMid));
    f[fAir]        = q8 (std::sqrt (m.air));
    f[fTonality]   = q8 (m.tonality);
    f[fLoudness]   = q8 (ramp (m.rmsDb, -50.0f, -6.0f));
    f[fWidth]      = q8 (m.width / 0.5f);
    f[fDensity]    = q8 (ramp (std::log2 (m.density + 0.01f), std::log2 (0.2f), 4.0f));
    f[fMotion]     = q8 (ramp (m.motionOct, 0.0f, 1.2f));
    f[fWarmth]     = q8 (ramp (10.0f * std::log10 ((m.low + m.mid + 1.0e-4f) / (m.highMid + m.air + 1.0e-4f)), 0.0f, 30.0f));

    r.tags = computeTags (m, r.category, r.isLoop, numChannels, f[fPunch]);
    return r;
}

} // namespace ss

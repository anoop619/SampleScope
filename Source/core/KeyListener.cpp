#include "KeyListener.h"

namespace ss
{

namespace
{
    constexpr int kFrame = 4096;
    constexpr int kHop = 2048;
    constexpr double kMemorySeconds = 60.0;     // older material fades out, so key changes are followed
}

KeyListener::KeyListener() : juce::Thread ("SS keylisten")
{
    for (auto& m : pendingMidi) m = 0;
    ring.resize ((size_t) fifo.getTotalSize());
    frame.resize ((size_t) kFrame);
    prepare (44100.0);
}

KeyListener::~KeyListener()
{
    stopThread (2000);
}

void KeyListener::prepare (double sampleRate)
{
    decimation = juce::jmax (1, (int) std::lround (sampleRate / 11025.0));
    analysisRate = sampleRate / decimation;

    // 4th-order Butterworth low-pass at ~40 % of the new Nyquist (anti-alias before decimating).
    auto design = [sampleRate, this] (Biquad& bq, double q)
    {
        const double fc = analysisRate * 0.4;
        const double w = 2.0 * dsp::pi * fc / sampleRate, c = std::cos (w), s = std::sin (w), al = s / (2.0 * q);
        const double a0 = 1.0 + al;
        bq.b0 = (float) ((1.0 - c) / 2.0 / a0); bq.b1 = (float) ((1.0 - c) / a0); bq.b2 = bq.b0;
        bq.a1 = (float) (-2.0 * c / a0); bq.a2 = (float) ((1.0 - al) / a0);
        bq.z1 = bq.z2 = 0.0f;
    };
    design (lp1, 0.5412);
    design (lp2, 1.3066);
    decimCounter = 0;
}

void KeyListener::pushAudio (const float* const* channels, int numChannels, int numSamples) noexcept
{
    if (! enabled.load (std::memory_order_relaxed) || numChannels <= 0)
        return;

    const float scale = 1.0f / (float) numChannels;
    for (int i = 0; i < numSamples; ++i)
    {
        float x = 0.0f;
        for (int c = 0; c < numChannels; ++c) x += channels[c][i];
        const float y = lp2.process (lp1.process (x * scale));

        if (++decimCounter >= decimation)
        {
            decimCounter = 0;
            int s1, n1, s2, n2;
            fifo.prepareToWrite (1, s1, n1, s2, n2);
            if (n1 > 0) { ring[(size_t) s1] = y; fifo.finishedWrite (1); }
        }
    }
}

void KeyListener::pushMidiNote (int note, float velocity) noexcept
{
    if (enabled.load (std::memory_order_relaxed) && note >= 0 && note < 128)
        pendingMidi[note % 12].fetch_add (juce::jmax (1, (int) (velocity * 100.0f)), std::memory_order_relaxed);
}

void KeyListener::setEnabled (bool shouldListen)
{
    enabled = shouldListen;
    if (shouldListen && ! isThreadRunning())
        startThread (juce::Thread::Priority::low);
    else if (! shouldListen && isThreadRunning())
        stopThread (2000);
}

void KeyListener::reset()
{
    resetRequested = true;
    key = Key::none;
    confidence = 0.0f;
    secondsHeard = 0.0f;
}

void KeyListener::run()
{
    while (! threadShouldExit())
    {
        if (resetRequested.exchange (false))
        {
            audioChroma.fill (0.0); midiChroma.fill (0.0);
            frameFill = 0;
            fifo.reset();
        }

        // Drain the FIFO into analysis frames.
        int s1, n1, s2, n2;
        fifo.prepareToRead (fifo.getNumReady(), s1, n1, s2, n2);
        auto consume = [this] (int start, int count)
        {
            for (int i = 0; i < count; ++i)
            {
                frame[(size_t) frameFill++] = ring[(size_t) (start + i)];
                if (frameFill == kFrame)
                {
                    processFrame (frame.data());
                    std::copy (frame.begin() + kHop, frame.end(), frame.begin());
                    frameFill = kFrame - kHop;
                }
            }
        };
        consume (s1, n1);
        consume (s2, n2);
        fifo.finishedRead (n1 + n2);

        // MIDI evidence decays on the same clock as audio.
        bool anyMidi = false;
        for (int pc = 0; pc < 12; ++pc)
        {
            const int v = pendingMidi[pc].exchange (0);
            if (v > 0) { midiChroma[(size_t) pc] += v / 100.0; anyMidi = true; }
        }
        juce::ignoreUnused (anyMidi);

        // Combine and estimate.
        std::array<float, 12> combined {};
        double aSum = 0, mSum = 0;
        for (int i = 0; i < 12; ++i) { aSum += audioChroma[(size_t) i]; mSum += midiChroma[(size_t) i]; }
        for (int i = 0; i < 12; ++i)
            combined[(size_t) i] = (float) ((aSum > 0 ? audioChroma[(size_t) i] / aSum : 0.0)
                                            + (mSum > 0 ? 0.7 * midiChroma[(size_t) i] / mSum : 0.0));

        if (secondsHeard.load() >= 4.0f || mSum >= 8.0)
        {
            float conf = 0.0f;
            const int k = dsp::estimateKey (combined, conf);
            key = k;
            confidence = conf;
        }

        wait (150);
    }
}

void KeyListener::processFrame (const float* f)
{
    double e = 0.0;
    for (int i = 0; i < kFrame; ++i) e += (double) f[i] * f[i];
    const double rms = std::sqrt (e / kFrame);

    const double hopSeconds = kHop / analysisRate;
    const double decay = std::exp (-hopSeconds / kMemorySeconds);
    for (auto& v : audioChroma) v *= decay;
    for (auto& v : midiChroma) v *= decay;

    if (rms < 1.0e-4)
        return;       // silence (transport stopped) teaches us nothing

    const float* mags = spectrum.compute (f, kFrame);
    dsp::findPeaks (mags, spectrum.numBins(), analysisRate / kFrame, 55.0f, 3500.0f, 0.03f, 24, peaks);

    std::array<float, 12> chroma {};
    dsp::addPeaksToChroma (peaks, 0.0f, chroma);
    float sum = 0.0f;
    for (auto v : chroma) sum += v;
    if (sum <= 0.0f)
        return;
    for (int i = 0; i < 12; ++i) audioChroma[(size_t) i] += chroma[(size_t) i] / sum;

    secondsHeard = secondsHeard.load() + (float) hopSeconds;
}

} // namespace ss

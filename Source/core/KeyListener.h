#pragma once

#include "DSPUtils.h"
#include "Types.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace ss
{

/** Works out the key of the project from what it hears.

    VST3 gives plug-ins the host tempo but not the project key (Ableton's Scale setting is
    only shared with Live's own devices). So SampleScope listens instead: put it on the
    master (or any track) and it builds a long-term pitch-class profile of the incoming
    audio, plus any MIDI routed to it, and matches that against key profiles.

    Audio thread: pushAudio()/pushMidiNote() only decimate and copy into a lock-free FIFO.
    The FFT work happens on a low-priority thread. */
class KeyListener : private juce::Thread
{
public:
    KeyListener();
    ~KeyListener() override;

    void prepare (double sampleRate);
    void pushAudio (const float* const* channels, int numChannels, int numSamples) noexcept;
    void pushMidiNote (int noteNumber, float velocity) noexcept;

    void setEnabled (bool shouldListen);
    bool isEnabled() const noexcept             { return enabled.load(); }
    void reset();

    int   getKey() const noexcept               { return key.load(); }
    float getConfidence() const noexcept        { return confidence.load(); }
    float getSecondsHeard() const noexcept      { return secondsHeard.load(); }

private:
    void run() override;
    void processFrame (const float* frame);

    // --- audio thread ---
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        inline float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };
    Biquad lp1, lp2;
    int decimation = 4, decimCounter = 0;
    double analysisRate = 11025.0;
    std::atomic<int> pendingMidi[12];

    juce::AbstractFifo fifo { 1 << 16 };
    std::vector<float> ring;

    // --- analysis thread ---
    dsp::Spectrum spectrum { 12 };       // 4096 points
    std::vector<float> frame, peaksScratch;
    int frameFill = 0;
    std::array<double, 12> audioChroma {}, midiChroma {};
    std::vector<dsp::Peak> peaks;
    std::atomic<bool> resetRequested { false };

    std::atomic<bool> enabled { false };
    std::atomic<int> key { Key::none };
    std::atomic<float> confidence { 0.0f }, secondsHeard { 0.0f };
};

} // namespace ss

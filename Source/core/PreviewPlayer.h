#pragma once

#include "RenderService.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace ss
{

/** The audition voice. Lock-free on the audio thread: new buffers are handed over through a
    try-locked slot, and old buffers are always released on a non-audio thread.

    When the host is playing and the sample is a tempo-synced loop, playback is phase-locked
    to the song position, so a loop you audition lands on the beat and stays there. */
class PreviewPlayer
{
public:
    struct HostState { bool playing = false; double ppq = 0.0; double bpm = 0.0; };

    /** Any non-audio thread. */
    void setBuffer (std::shared_ptr<const PreviewBuffer> buffer, bool startPlaying);
    void play()                    { playRequest = true; }
    void stop()                    { stopRequest = true; }
    void setGain (float g)         { gain = g; }
    void setSyncToHost (bool s)    { syncToHost = s; }
    bool isPlaying() const         { return playingFlag.load(); }
    float getProgress() const      { return progress.load(); }
    uint32_t getCurrentId() const  { return currentId.load(); }

    /** Message thread, from a timer: frees buffers the audio thread no longer uses. */
    void collectGarbage();

    void prepare (double sampleRate);

    /** Audio thread. Adds the preview into `out`. */
    void process (juce::AudioBuffer<float>& out, int numSamples, const HostState& host) noexcept;

private:
    juce::SpinLock swapLock;
    std::shared_ptr<const PreviewBuffer> pending, current;
    std::atomic<bool> hasPending { false }, pendingPlay { false };

    juce::CriticalSection keepAliveLock;
    std::vector<std::shared_ptr<const PreviewBuffer>> keepAlive;

    std::atomic<bool> playRequest { false }, stopRequest { false }, syncToHost { true };
    std::atomic<bool> playingFlag { false };
    std::atomic<float> gain { 0.8f }, progress { 0.0f };
    std::atomic<uint32_t> currentId { 0 };

    // audio-thread only
    bool playing = false, stopping = false;
    double position = 0.0;
    float fade = 0.0f, fadeStep = 0.0f;
    juce::SmoothedValue<float> smoothedGain { 0.8f };
};

} // namespace ss

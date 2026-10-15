#include "PreviewPlayer.h"

namespace ss
{

void PreviewPlayer::setBuffer (std::shared_ptr<const PreviewBuffer> buffer, bool startPlaying)
{
    {
        const juce::ScopedLock sl (keepAliveLock);
        if (buffer != nullptr)
            keepAlive.push_back (buffer);     // the audio thread never drops the last reference
    }
    {
        const juce::SpinLock::ScopedLockType sl (swapLock);
        pending = std::move (buffer);
        pendingPlay = startPlaying;
        hasPending = true;
    }
    collectGarbage();
}

void PreviewPlayer::collectGarbage()
{
    const juce::ScopedLock sl (keepAliveLock);
    // Keep the newest few; drop any buffer nobody else references any more.
    for (int i = (int) keepAlive.size(); --i >= 0;)
        if (keepAlive[(size_t) i].use_count() == 1 && i < (int) keepAlive.size() - 2)
            keepAlive.erase (keepAlive.begin() + i);
}

void PreviewPlayer::prepare (double sampleRate)
{
    smoothedGain.reset (sampleRate, 0.03);
    fadeStep = 1.0f / (float) std::max (1.0, sampleRate * 0.004);   // 4 ms fades
}

void PreviewPlayer::process (juce::AudioBuffer<float>& out, int numSamples, const HostState& host) noexcept
{
    if (hasPending.load (std::memory_order_acquire))
    {
        const juce::SpinLock::ScopedTryLockType tl (swapLock);
        if (tl.isLocked())
        {
            current = pending;          // keepAlive still owns a reference: no free() here
            pending = nullptr;
            hasPending = false;
            currentId = current != nullptr ? current->id : 0u;
            position = 0.0;
            fade = 0.0f;
            stopping = false;
            playing = current != nullptr && pendingPlay.load();
        }
    }

    if (playRequest.exchange (false) && current != nullptr)
    {
        playing = true; stopping = false; position = 0.0; fade = 0.0f;
    }
    if (stopRequest.exchange (false) && playing)
        stopping = true;

    if (! playing || current == nullptr || current->audio.getNumSamples() == 0)
    {
        playingFlag = false;
        return;
    }

    const auto& audio = current->audio;
    const int len = audio.getNumSamples();
    const int srcCh = audio.getNumChannels();
    const int outCh = out.getNumChannels();

    // Phase-lock loops to the song position while the host plays.
    const bool locked = syncToHost.load() && host.playing && current->isLoop && current->loopBeats > 0.0 && host.bpm > 0.0;
    if (locked)
    {
        double beats = std::fmod (host.ppq, current->loopBeats);
        if (beats < 0) beats += current->loopBeats;
        const double expected = beats / current->loopBeats * len;
        if (std::abs (expected - position) > 64.0)
            position = expected;
    }

    smoothedGain.setTargetValue (gain.load());
    int pos = (int) position;

    for (int i = 0; i < numSamples; ++i)
    {
        if (pos >= len)
        {
            if (current->isLoop) pos -= len;
            else { playing = false; break; }
        }

        if (stopping)
        {
            fade -= fadeStep;
            if (fade <= 0.0f) { playing = false; stopping = false; fade = 0.0f; break; }
        }
        else if (fade < 1.0f)
        {
            fade = std::min (1.0f, fade + fadeStep);
        }

        const float g = smoothedGain.getNextValue() * fade;
        for (int c = 0; c < outCh; ++c)
            out.addSample (c, i, g * audio.getSample (std::min (c, srcCh - 1), pos));
        ++pos;
    }

    position = pos;
    progress = len > 0 ? (float) (position / len) : 0.0f;
    playingFlag = playing;
}

} // namespace ss

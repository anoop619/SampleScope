#pragma once

#include "Theme.h"

namespace ss::ui
{

/** Bottom bar: play, tempo, project key, sync toggles, transpose, warp algorithm, volume. */
class SyncBar : public juce::Component
{
public:
    SyncBar();

    IconButton  playButton { "Play", IconButton::Icon::Play };
    juce::Slider manualBpm;                  // only used when the host gives no tempo
    juce::ComboBox keyBox, warpBox;
    PillToggle  syncTempo { "Sync BPM" }, syncKey { "Sync key" }, autoPlay { "Auto-play" };
    juce::Slider transpose, volume;

    /** Host tempo display ("128.00", "HOST") or manual tempo entry. */
    void setTempo (double bpm, bool fromHost);
    /** e.g. "A minor · 82%" when detecting, or "" for a manual key. */
    void setKeyStatus (const juce::String& s, bool confident);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Label tempoValue, keyStatus;
    juce::String tempoSource;
    double shownBpm = -1.0;
    bool keyConfident = false;
    juce::Rectangle<int> tempoArea, keyArea, transposeArea, warpArea, volArea;
};

} // namespace ss::ui

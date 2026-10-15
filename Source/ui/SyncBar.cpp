#include "SyncBar.h"
#include "core/Warper.h"

namespace ss::ui
{

SyncBar::SyncBar()
{
    addAndMakeVisible (playButton);
    playButton.setTooltip ("Play / stop the preview (double-click a row or press Return)");

    tempoValue.setFont (monoFont (17.0f));
    tempoValue.setColour (juce::Label::textColourId, Colours::text);
    tempoValue.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (tempoValue);

    manualBpm.setSliderStyle (juce::Slider::LinearBar);
    manualBpm.setRange (40.0, 250.0, 0.01);
    manualBpm.setTextValueSuffix (" BPM");
    manualBpm.setNumDecimalPlacesToDisplay (2);
    manualBpm.setColour (juce::Slider::trackColourId, Colours::accent.withAlpha (0.25f));
    manualBpm.setTooltip ("This host doesn't report its tempo - set it here (drag or double-click to type)");
    addChildComponent (manualBpm);

    keyBox.addItem ("Detect from audio", 1);
    keyBox.addSeparator();
    for (int k = 0; k < 24; ++k)
        keyBox.addItem (Key::longName (k), k + 2);
    keyBox.setTooltip ("Project key. 'Detect' listens to the audio going through SampleScope (put it on your master) "
                       "and to any MIDI routed to it.");
    addAndMakeVisible (keyBox);

    keyStatus.setFont (font (11.5f));
    keyStatus.setColour (juce::Label::textColourId, Colours::dim);
    addAndMakeVisible (keyStatus);

    for (auto* b : { &syncTempo, &syncKey, &autoPlay })
        addAndMakeVisible (b);
    syncTempo.setTooltip ("Time-stretch loops to the project tempo");
    syncKey.setTooltip ("Transpose pitched samples to the project key (relative major/minor counts as in key)");
    autoPlay.setTooltip ("Play samples as soon as you select them");

    transpose.setSliderStyle (juce::Slider::IncDecButtons);
    transpose.setRange (-12, 12, 1);
    transpose.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, 26);
    transpose.textFromValueFunction = [] (double v) { return (v > 0 ? "+" : "") + juce::String ((int) v) + " st"; };
    transpose.valueFromTextFunction = [] (const juce::String& t) { return (double) t.retainCharacters ("-0123456789").getIntValue(); };
    transpose.setTooltip ("Extra transposition on top of key sync");
    addAndMakeVisible (transpose);

    for (int m = 0; m <= (int) WarpMode::Beats; ++m)
        warpBox.addItem (warpModeName ((WarpMode) m), m + 1);
    warpBox.setTooltip ("Auto: Beats for drum loops, Pro for melodic loops, Pro + Formants for vocals, Re-Pitch "
                        "(sampler-style, cleanest) for short one-shots");
    addAndMakeVisible (warpBox);

    volume.setSliderStyle (juce::Slider::LinearHorizontal);
    volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    volume.setRange (0.0, 1.0);
    volume.setTooltip ("Preview volume");
    addAndMakeVisible (volume);
}

void SyncBar::setTempo (double bpm, bool fromHost)
{
    const auto source = fromHost ? "HOST" : "MANUAL";
    if (tempoSource == source && std::abs (bpm - shownBpm) < 0.005)
        return;
    shownBpm = bpm;
    tempoSource = source;
    tempoValue.setVisible (fromHost);
    manualBpm.setVisible (! fromHost);
    if (fromHost)
        tempoValue.setText (juce::String (bpm, 2), juce::dontSendNotification);
    repaint (tempoArea);
}

void SyncBar::setKeyStatus (const juce::String& s, bool confident)
{
    if (s == keyStatus.getText() && confident == keyConfident)
        return;
    keyConfident = confident;
    keyStatus.setText (s, juce::dontSendNotification);
    keyStatus.setColour (juce::Label::textColourId, confident ? Colours::accent : Colours::dim);
}

void SyncBar::paint (juce::Graphics& g)
{
    g.setColour (Colours::faint);
    g.setFont (font (10.0f, true));
    auto caption = [&g] (juce::Rectangle<int> area, const juce::String& t)
    {
        g.drawText (t, area.withHeight (12).translated (0, -13), juce::Justification::centredLeft);
    };
    caption (tempoArea, "TEMPO" + sep() + tempoSource);
    caption (keyArea, "PROJECT KEY");
    caption (transposeArea, "TRANSPOSE");
    caption (warpArea, "WARP");
    if (! volArea.isEmpty()) caption (volArea, "PREVIEW VOLUME");
}

void SyncBar::resized()
{
    const int h = 28;
    auto row = getLocalBounds().withTrimmedTop (14).withHeight (h);
    const bool compact = getWidth() < 950;
    syncTempo.setButtonText (compact ? "BPM" : "Sync BPM");
    syncKey.setButtonText (compact ? "Key" : "Sync key");
    autoPlay.setButtonText (compact ? "Auto" : "Auto-play");

    playButton.setBounds (row.removeFromLeft (h + 4));
    row.removeFromLeft (12);

    tempoArea = row.removeFromLeft (104);
    tempoValue.setBounds (tempoArea);
    manualBpm.setBounds (tempoArea.reduced (0, 1));
    row.removeFromLeft (10);

    keyArea = row.removeFromLeft (148);
    keyBox.setBounds (keyArea);
    keyStatus.setBounds (keyArea.translated (0, h + 1).withHeight (14).withWidth (240));
    row.removeFromLeft (10);

    syncTempo.setBounds (row.removeFromLeft (compact ? 62 : 96));
    row.removeFromLeft (6);
    syncKey.setBounds (row.removeFromLeft (compact ? 56 : 90));
    row.removeFromLeft (12);

    transposeArea = row.removeFromLeft (100);
    transpose.setBounds (transposeArea);
    row.removeFromLeft (10);

    warpArea = row.removeFromLeft (128);
    warpBox.setBounds (warpArea);
    row.removeFromLeft (10);

    autoPlay.setBounds (row.removeFromRight (compact ? 64 : 96));
    row.removeFromRight (12);
    const bool roomForVolume = row.getWidth() >= 70;
    volume.setVisible (roomForVolume);
    volArea = roomForVolume ? row.removeFromRight (juce::jmin (130, row.getWidth())) : juce::Rectangle<int>();
    volume.setBounds (volArea);
}

} // namespace ss::ui

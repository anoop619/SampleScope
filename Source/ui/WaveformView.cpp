#include "WaveformView.h"

namespace ss::ui
{

void WaveformView::setSample (std::shared_ptr<const RenderedSample> r, Category c)
{
    sample = std::move (r);
    category = c;
    if (sample != nullptr)
    {
        title = sample->displayName;
        subtitle = sample->error.isNotEmpty() ? sample->error : sample->plan.describe();
    }
    busy = false;
    rebuildPeaks();
    repaint();
}

void WaveformView::setPendingTitle (const juce::String& t, Category c)
{
    title = t;
    category = c;
    subtitle = {};
    busy = true;
    repaint();
}

void WaveformView::setPlayhead (float p, bool isPlaying)
{
    if (std::abs (p - playhead) > 0.001f || isPlaying != playing)
    {
        playhead = p;
        playing = isPlaying;
        repaint();
    }
}

void WaveformView::rebuildPeaks()
{
    const int w = std::max (1, getWidth() - 24);
    mins.assign ((size_t) w, 0.0f);
    maxs.assign ((size_t) w, 0.0f);
    if (sample == nullptr || sample->audio.getNumSamples() == 0)
        return;

    const auto& a = sample->audio;
    const int n = a.getNumSamples();
    for (int x = 0; x < w; ++x)
    {
        const int s0 = (int) ((int64_t) x * n / w), s1 = std::max (s0 + 1, (int) ((int64_t) (x + 1) * n / w));
        float lo = 0.0f, hi = 0.0f;
        for (int c = 0; c < a.getNumChannels(); ++c)
        {
            const auto range = juce::FloatVectorOperations::findMinAndMax (a.getReadPointer (c, s0), s1 - s0);
            lo = std::min (lo, range.getStart());
            hi = std::max (hi, range.getEnd());
        }
        mins[(size_t) x] = lo;
        maxs[(size_t) x] = hi;
    }
}

void WaveformView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (Colours::line);
    g.drawRoundedRectangle (r, 10.0f, 1.0f);

    auto inner = getLocalBounds().reduced (12, 8);
    auto header = inner.removeFromTop (20);

    if (title.isEmpty())
    {
        g.setColour (Colours::faint);
        g.setFont (font (13.0f));
        g.drawText ("Select a sample to preview it, synced to your project", inner, juce::Justification::centred);
        return;
    }

    g.setColour (categoryColour (category));
    g.fillEllipse ((float) header.getX(), (float) header.getCentreY() - 4.0f, 8.0f, 8.0f);
    header.removeFromLeft (14);
    g.setColour (Colours::text);
    g.setFont (font (14.0f, true));
    const int titleW = std::min (header.getWidth() / 2, juce::GlyphArrangement::getStringWidthInt (font (14.0f, true), title) + 16);
    g.drawText (title, header.removeFromLeft (titleW), juce::Justification::centredLeft, true);
    g.setColour (busy ? Colours::warn : Colours::dim);
    g.setFont (font (12.5f));
    g.drawText (busy ? juce::String ("Rendering...") : subtitle, header, juce::Justification::centredLeft, true);

    inner.removeFromTop (4);
    const float mid = (float) inner.getCentreY();
    const float amp = (float) inner.getHeight() * 0.5f;
    const int w = (int) mins.size();
    const int playX = (int) (playhead * (float) w);
    const auto colour = categoryColour (category);

    for (int x = 0; x < w && x < inner.getWidth(); ++x)
    {
        const float top = mid - maxs[(size_t) x] * amp;
        const float bottom = mid - mins[(size_t) x] * amp;
        g.setColour (playing && x <= playX ? colour : colour.withAlpha (busy ? 0.25f : 0.55f));
        g.fillRect ((float) (inner.getX() + x), top, 1.0f, std::max (1.0f, bottom - top));
    }

    if (playing)
    {
        g.setColour (Colours::text);
        g.fillRect ((float) (inner.getX() + playX), (float) inner.getY(), 1.5f, (float) inner.getHeight());
    }
}

//==============================================================================
void DragTile::setInfo (const juce::String& d, bool enabled)
{
    if (d != detail || enabled != ready)
    {
        detail = d;
        ready = enabled;
        repaint();
    }
}

void DragTile::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool over = isMouseOver() && ready;
    g.setColour (ready ? Colours::accent.withAlpha (over ? 0.24f : 0.14f) : Colours::panel);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (ready ? Colours::accent.withAlpha (0.8f) : Colours::line);
    const float dash[] = { 5.0f, 4.0f };
    juce::Path outline;
    outline.addRoundedRectangle (r.reduced (1.0f), 9.0f);
    juce::Path dashed;
    juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dash, 2);
    g.fillPath (dashed);

    // Arrow icon
    auto icon = r.withSizeKeepingCentre (22.0f, 22.0f).withY (r.getY() + r.getHeight() * 0.18f);
    juce::Path arrow;
    arrow.startNewSubPath (icon.getCentreX(), icon.getY());
    arrow.lineTo (icon.getCentreX(), icon.getBottom() - 4.0f);
    arrow.startNewSubPath (icon.getX() + 4.0f, icon.getBottom() - 10.0f);
    arrow.lineTo (icon.getCentreX(), icon.getBottom() - 4.0f);
    arrow.lineTo (icon.getRight() - 4.0f, icon.getBottom() - 10.0f);
    arrow.startNewSubPath (icon.getX(), icon.getBottom());
    arrow.lineTo (icon.getRight(), icon.getBottom());
    g.setColour (ready ? Colours::accent : Colours::faint);
    g.strokePath (arrow, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    auto text = r.withTrimmedTop (r.getHeight() * 0.5f).reduced (8.0f, 2.0f);
    g.setColour (ready ? Colours::text : Colours::faint);
    g.setFont (font (13.0f, true));
    g.drawText ("Drag to DAW", text.removeFromTop (18.0f), juce::Justification::centred);
    g.setColour (Colours::dim);
    g.setFont (font (11.0f));
    g.drawFittedText (detail, text.toNearestInt(), juce::Justification::centredTop, 2);
}

void DragTile::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || ! ready || e.getDistanceFromDragStart() < 4 || ! getFileForDrag)
        return;
    dragging = true;
    const auto f = getFileForDrag();
    if (f.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this,
                                                                    [safe = juce::Component::SafePointer<DragTile> (this)]
                                                                    { if (safe != nullptr) safe->dragging = false; });
    else
        dragging = false;
}

} // namespace ss::ui

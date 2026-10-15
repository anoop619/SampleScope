#pragma once

#include "Theme.h"
#include "core/RenderService.h"

namespace ss::ui
{

/** Waveform of the (warped) selected sample, with what was done to it and a playhead. */
class WaveformView : public juce::Component
{
public:
    void setSample (std::shared_ptr<const RenderedSample> rendered, Category category);
    void setPendingTitle (const juce::String& title, Category category);
    void setBusy (bool b)                         { if (busy != b) { busy = b; repaint(); } }
    void setPlayhead (float progress, bool playing);

    std::function<void()> onClick;

    void paint (juce::Graphics&) override;
    void resized() override                       { rebuildPeaks(); }
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }

private:
    void rebuildPeaks();

    std::shared_ptr<const RenderedSample> sample;
    juce::String title, subtitle;
    Category category = Category::Unknown;
    std::vector<float> mins, maxs;
    float playhead = 0.0f;
    bool playing = false, busy = false;
};

/** "Drag to DAW" tile: starts a native file drag of the rendered (or original) file. */
class DragTile : public juce::Component
{
public:
    std::function<juce::File()> getFileForDrag;
    void setInfo (const juce::String& detail, bool enabled);

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override    { dragging = false; }

private:
    juce::String detail { "Select a sample" };
    bool ready = false, dragging = false;
};

} // namespace ss::ui
